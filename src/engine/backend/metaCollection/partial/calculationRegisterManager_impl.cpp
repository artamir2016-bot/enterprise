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
