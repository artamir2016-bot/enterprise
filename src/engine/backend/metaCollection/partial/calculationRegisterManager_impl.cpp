////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : calculationRegister manager
////////////////////////////////////////////////////////////////////////////

#include "calculationRegister.h"
#include "calculationRegisterManager.h"

#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — From() + Where materialises the read through L3
#include "backend/query/dbTableProvider.h"    // ibDbTableProvider::GetValueAttribute — the DB value-assembly
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 — structured IR
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFilterPredicate (shared lowering)
#include "backend/calculation/actionPeriodDisplacement.h"            // ibComputeActionPeriodDisplacement — the tested kernel
#include "backend/calculation/calculationBase.h"                     // ibComputeBaseContributions — the tested base kernel
#include "backend/metaCollection/dimension/metaDimensionObject.h"     // ibValueMetaObjectDimension (GetBase matching)
#include "backend/metaCollection/resource/metaResourceObject.h"       // ibValueMetaObjectResource (GetBase value columns)

#include <map>

ibValue ibValueManagerDataObjectCalculationRegister::Get(const ibValue& cFilter)
{
	ibRequireOpenBase();

	ibValueModelTable* retTable = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = colCollection->AddColumn(object->GetName(), object->GetTypeDesc(), object->GetSynonym());
		colInfo->SetColumnID(object->GetMetaID());
	}

	// The Structure a script passes becomes the condition here — the SAME converter the query door
	// uses, so a script's filter and a query's condition are one thing from this point on.
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter);

	// Filtered read through the L3 door: each selected dimension is an Eq condition,
	// decomposed inside L3 across its physical fields. Rows come from the L3
	// selection (GetValue) — no statement, no raw result set here.
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
					q.Where(filter);
		ibReadPageRequest page;
		page.m_count = 0;   // every matching record
		ibDataQueryResult selection = q.Execute(page);
		while (selection.Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				retLine->SetValueByMetaID(object->GetMetaID(), selection.GetValue(object));
			wxDELETE(retLine);
		}
	}
	catch (...) {}

	return retTable;
}

// GetDisplacement(Filter) — the manager face of the displacement kernel. It reads the filtered records
// (their own attributes plus the action-period columns), computes each record's ACTUAL action period by
// subtracting the action periods of higher-priority records, and returns the records with the
// ActualActionPeriodStart/End columns filled from that computation. This is the same result the record-set
// write hook stores per set; here it is offered as a query over an arbitrary filtered selection, so a
// script can ask "what does the picture look like after displacement" without opening a set.
ibValue ibValueManagerDataObjectCalculationRegister::GetDisplacement(const ibValue& cFilter)
{
	ibRequireOpenBase();

	ibValueModelTable* retTable = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo =
			colCollection->AddColumn(object->GetName(), object->GetTypeDesc(), object->GetSynonym());
		colInfo->SetColumnID(object->GetMetaID());
	}

	// No action period -> displacement is undefined; return the empty (only-columns) table rather than
	// silently reading rows whose ActualActionPeriod columns do not exist.
	if (!m_metaObject->IsUseActionPeriod())
		return retTable;

	const ibMetaID apStart  = m_metaObject->GetActionPeriodStart()->GetMetaID();
	const ibMetaID apEnd    = m_metaObject->GetActionPeriodEnd()->GetMetaID();
	const ibMetaID aapStart = m_metaObject->GetActualActionPeriodStart()->GetMetaID();
	const ibMetaID aapEnd   = m_metaObject->GetActualActionPeriodEnd()->GetMetaID();

	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter);

	// One filtered pass through the L3 door, collecting each row's model line together with its action
	// period. Priority = read order (the interim rule the write hook uses); the per-calculation-type
	// priority from the chart's displacing lists replaces only this line once predefined data is imported.
	std::vector<ibActionPeriodRecord> recs;
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		q.Where(filter);
		ibReadPageRequest page;
		page.m_count = 0;   // every matching record
		ibDataQueryResult selection = q.Execute(page);
		while (selection.Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				retLine->SetValueByMetaID(object->GetMetaID(), selection.GetValue(object));
			ibValue s, e;
			retLine->GetValueByMetaID(apStart, s);
			retLine->GetValueByMetaID(apEnd, e);
			recs.push_back({ /*priority*/ (int64_t)recs.size(), (int64_t)s.GetDate(), (int64_t)e.GetDate() });
			wxDELETE(retLine);
		}
	}
	catch (...) {}

	// Displace, then write each row's actual span back into its line. A fully displaced record collapses
	// to a zero-length span at its original start (same convention as the record-set hook).
	const std::vector<std::vector<ibActionInterval>> actual = ibComputeActionPeriodDisplacement(recs);
	const long n = retTable->GetRowCount();
	for (long i = 0; i < n && (size_t)i < actual.size(); ++i) {
		int64_t as, ae;
		if (actual[i].empty()) {
			as = recs[i].start;
			ae = recs[i].start;
		} else {
			as = actual[i].front().start;
			ae = actual[i].back().end;
		}
		ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt((long)i);
		wxASSERT(retLine);
		retLine->SetValueByMetaID(aapStart, ibValue(wxDateTime(wxLongLong(as))));
		retLine->SetValueByMetaID(aapEnd,   ibValue(wxDateTime(wxLongLong(ae))));
		wxDELETE(retLine);
	}

	return retTable;
}

ibValue ibValueManagerDataObjectCalculationRegister::Get(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibRequireOpenBase();

	ibValueModelTable* retTable = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				object->GetName(),
				object->GetTypeDesc(),
				object->GetSynonym()
			);
		colInfo->SetColumnID(object->GetMetaID());
	}

	// A calculation register is always dated — the period is always a valid filter here.
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter);

	// Period + dimension filtered read through the L3 door: the period is an Eq
	// condition like any selected dimension; L3 decomposes each across its physical
	// fields and binds them. Rows come from the L3 selection (GetValue) — no raw
	// statement, no per-DBMS SQL here.
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		q.Where(m_metaObject->GetRegisterPeriod(), ibQueryFilterOp::Equal, cPeriod);
						q.Where(filter);
		ibReadPageRequest page;
		page.m_count = 0;   // every matching record
		ibDataQueryResult selection = q.Execute(page);
		while (selection.Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				retLine->SetValueByMetaID(object->GetMetaID(), selection.GetValue(object));
			wxDELETE(retLine);
		}
	}
	catch (...) {}

	return retTable;
}

// GetBase(BaseRegister, Filter) — see the header. The base register is read ONCE (its records, their
// action periods and resources), displaced ONCE, then every dependent record is scored against that
// in-memory picture. Matching is by shared-name dimension VALUES: a base record contributes to a
// dependent record only when every dimension they have in common holds an equal value.
ibValue ibValueManagerDataObjectCalculationRegister::GetBase(const ibValue& cBaseRegister, const ibValue& cFilter)
{
	ibRequireOpenBase();

	// The base is meaningful only when the dependent register has a base period and the base register has
	// an action period to weigh against it. Missing either -> an empty (columns-only) table, not a guess.
	ibValueManagerDataObjectCalculationRegister* baseMgr =
		cBaseRegister.ConvertToType<ibValueManagerDataObjectCalculationRegister>();
	const ibValueMetaObjectCalculationRegister* baseMeta = baseMgr ? baseMgr->GetMetaObject() : nullptr;

	// Base resources become the value columns; a "Base<Resource>" name and a synthetic id (base resource
	// metaID high-bit) keep them from colliding with the dependent register's own attribute columns.
	std::vector<ibValueMetaObjectResource*> baseResources =
		baseMeta ? baseMeta->GetResourceArrayObject() : std::vector<ibValueMetaObjectResource*>();

	ibValueModelTable* retTable = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		auto* colInfo = colCollection->AddColumn(object->GetName(), object->GetTypeDesc(), object->GetSynonym());
		colInfo->SetColumnID(object->GetMetaID());
	}
	for (const auto res : baseResources) {
		auto* colInfo = colCollection->AddColumn(wxT("Base") + res->GetName(), res->GetTypeDesc(), res->GetSynonym());
		colInfo->SetColumnID(res->GetMetaID() | 0x40000000);
	}

	if (baseMeta == nullptr || !m_metaObject->IsUseBasePeriod() || !baseMeta->IsUseActionPeriod())
		return retTable;

	// ---- read the base register once: dimensions (by name), action period, resource values ----------
	struct BaseRow {
		std::map<wxString, ibValue> dims;
		std::vector<ibNumber> res;   // parallel to baseResources
	};
	std::vector<BaseRow> baseRows;
	std::vector<ibActionPeriodRecord> baseRecs;
	const std::vector<ibValueMetaObjectDimension*> baseDims = baseMeta->GetDimensionArrayObject();
	try {
		ibDataQueryBuilder q;
		q.From(baseMeta->GetQueryable());
		ibReadPageRequest page;
		page.m_count = 0;
		ibDataQueryResult sel = q.Execute(page);
		while (sel.Next()) {
			BaseRow row;
			for (const auto d : baseDims)
				row.dims[d->GetName()] = sel.GetValue(d);
			for (const auto r : baseResources)
				row.res.push_back(sel.GetValue(r).GetNumber());
			baseRecs.push_back({ /*priority*/ (int64_t)baseRecs.size(),
			                     (int64_t)sel.GetValue(baseMeta->GetActionPeriodStart()).GetDate(),
			                     (int64_t)sel.GetValue(baseMeta->GetActionPeriodEnd()).GetDate() });
			baseRows.push_back(std::move(row));
		}
	}
	catch (...) {}

	// Displace the base register's action periods once. actual[i] pairs with baseRows[i].
	const std::vector<std::vector<ibActionInterval>> baseActual = ibComputeActionPeriodDisplacement(baseRecs);

	// ---- score each dependent record against the base picture ----------------------------------------
	const ibMetaID basePS = m_metaObject->GetBasePeriodStart()->GetMetaID();
	const ibMetaID basePE = m_metaObject->GetBasePeriodEnd()->GetMetaID();
	const std::vector<ibValueMetaObjectDimension*> selfDims = m_metaObject->GetDimensionArrayObject();
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter);
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		q.Where(filter);
		ibReadPageRequest page;
		page.m_count = 0;
		ibDataQueryResult sel = q.Execute(page);
		while (sel.Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				retLine->SetValueByMetaID(object->GetMetaID(), sel.GetValue(object));

			ibValue bs, be;
			retLine->GetValueByMetaID(basePS, bs);
			retLine->GetValueByMetaID(basePE, be);
			const int64_t queryStart = (int64_t)bs.GetDate();
			const int64_t queryEnd   = (int64_t)be.GetDate();

			// This dependent record's dimension values, by name, for matching against base records.
			std::map<wxString, ibValue> selfDimVals;
			for (const auto d : selfDims)
				selfDimVals[d->GetName()] = sel.GetValue(d);

			// Which base rows match on every shared-name dimension, and their actual periods.
			std::vector<int> matched;
			std::vector<std::vector<ibActionInterval>> matchedActual;
			for (size_t i = 0; i < baseRows.size(); ++i) {
				bool ok = true;
				for (const auto& kv : baseRows[i].dims) {
					auto it = selfDimVals.find(kv.first);
					if (it != selfDimVals.end() && !(it->second == kv.second)) { ok = false; break; }
				}
				if (!ok)
					continue;
				matched.push_back((int)i);
				matchedActual.push_back(baseActual[i]);
			}

			const std::vector<ibBaseContribution> contrib =
				ibComputeBaseContributions(queryStart, queryEnd, matchedActual);

			// Weighted resource sums: value * overlap / total, over the matched base rows.
			for (size_t c = 0; c < baseResources.size(); ++c) {
				ibNumber sum(0);
				for (size_t m = 0; m < matched.size(); ++m) {
					const ibBaseContribution& bc = contrib[m];
					if (bc.totalLength <= 0 || bc.overlapLength <= 0)
						continue;
					sum += baseRows[matched[m]].res[c] * ibNumber((long long)bc.overlapLength) / ibNumber((long long)bc.totalLength);
				}
				retLine->SetValueByMetaID(baseResources[c]->GetMetaID() | 0x40000000, ibValue(sum));
			}
			wxDELETE(retLine);
		}
	}
	catch (...) {}

	return retTable;
}
