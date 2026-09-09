////////////////////////////////////////////////////////////////////////////
//	Description : Recalculation (Перерасчёт) — the metaobject: ctor, lifecycle
//	              events, ReadData/WriteData, plus the vended L4 queryable and its
//	              source descriptor. Shaped after the DB-backed tabular section
//	              (metaTableObject.cpp): a subordinate that vends a queryable +
//	              a parent-qualified physical table and registers as a source on run.
////////////////////////////////////////////////////////////////////////////

#include "recalculation.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "backend/clsid.h"                     // reference_to_clsid — the synthetic reference columns' targets
#include "backend/query/columnLayout.h"        // ibFieldSuffix / field naming helpers

//***********************************************************************
//*                   ibRecalculationQueryable                          *
//***********************************************************************

// The RECALCULATION OBJECT — the record (recorder) being recalculated, held as one reference field.
// A recalculation belongs to exactly one calculation register, so the register itself is the constant
// target and the type rides on the column rather than per row.
const ibBackendQueryColumn* ibRecalculationQueryable::RecalculationObjectColumn() const
{
	if (!m_recalcObject) {
		const ibValueMetaObject* owner = m_meta != nullptr ? m_meta->GetParent() : nullptr;
		// A unique, stable id derived from the recalculation's own metaID (high bit clear of the
		// dimension attribute ids in the same table). Vended, never declared as a logical column.
		const ibMetaID identity = m_meta != nullptr ? (m_meta->GetMetaID() | 0x20000000) : 0;
		m_recalcObject.reset(new ibRawDBColumn(ibRawDBColumn::Reference(
			wxT("recalcobj"),
			owner != nullptr ? reference_to_clsid(owner->GetMetaID()) : 0,
			wxT("RecalculationObject"), identity)));
	}
	return m_recalcObject.get();
}

// The CALCULATION TYPE — which calculation type this recalculation row concerns. Untyped here
// (structurally a reference); the bound chart of calculation types is wired by the register.
const ibBackendQueryColumn* ibRecalculationQueryable::CalculationTypeColumn() const
{
	if (!m_calcType) {
		const ibMetaID identity = m_meta != nullptr ? (m_meta->GetMetaID() | 0x40000000) : 0;
		m_calcType.reset(new ibRawDBColumn(ibRawDBColumn::Reference(
			wxT("calctype"), 0, wxT("CalculationType"), identity)));
	}
	return m_calcType.get();
}

const ibBackendQueryColumn* ibRecalculationQueryable::ResolveColumnByName(const wxString& name) const
{
	if (name.IsSameAs(wxT("RecalculationObject"), false))
		return RecalculationObjectColumn();
	if (name.IsSameAs(wxT("CalculationType"), false))
		return CalculationTypeColumn();
	return m_meta->FindObjectByFilter<ibValueMetaObjectDimension>(name, { g_metaDimensionCLSID });
}

std::vector<const ibBackendQueryColumn*> ibRecalculationQueryable::GetColumns() const
{
	std::vector<const ibBackendQueryColumn*> columns;
	columns.push_back(RecalculationObjectColumn());
	columns.push_back(CalculationTypeColumn());
	for (const auto dimension : m_meta->GetDimensionArrayObject())
		columns.push_back(dimension);
	return columns;
}

wxString ibRecalculationQueryable::GetQueryTableName() const { return m_meta->GetPhysicalTableName(); }
ibGuid   ibRecalculationQueryable::GetQueryTableGuid() const { return m_meta->GetGuid(); }
wxString ibRecalculationQueryable::GetQueryName()      const { return m_meta->GetName(); }
ibMetaID ibRecalculationQueryable::GetQueryTableId()   const { return m_meta->GetMetaID(); }
const ibMetaData* ibRecalculationQueryable::GetMetaData() const { return m_meta->GetMetaData(); }
// Identity = the recalculation object then the dimension columns — the row's natural key. No line
// number and no uuid: a recalculation row is keyed by (object, dimensions).
std::vector<ibQuerySortItem> ibRecalculationQueryable::GetIdentitySort() const
{
	std::vector<ibQuerySortItem> sort;
	sort.push_back(ibQuerySortItem{ RecalculationObjectColumn(), true });
	for (const auto dimension : m_meta->GetDimensionArrayObject())
		sort.push_back(ibQuerySortItem{ dimension, true });
	return sort;
}

//***********************************************************************
//*                 ibRecalculationSourceDescriptor                     *
//***********************************************************************

ibRecalculationSourceDescriptor::ibRecalculationSourceDescriptor(ibValueMetaObjectRecalculation* meta)
	: m_meta(meta), m_queryable(meta)
{
}

wxString ibRecalculationSourceDescriptor::GetNamespace() const
{
	// parent-qualified: the recalculation's namespace is its parent calculation register's kind.
	ibValueMetaObject* parent = m_meta->GetParent();
	return parent != nullptr ? ibValue::GetNameObjectFromID(parent->GetClassType()) : wxString();
}

wxString ibRecalculationSourceDescriptor::GetName() const
{
	// "<Register>.<Recalculation>" — reached as the 3-segment source.
	ibValueMetaObject* parent = m_meta->GetParent();
	return parent != nullptr ? (parent->GetName() + wxT(".") + m_meta->GetName()) : m_meta->GetName();
}

const ibBackendQueryable* ibRecalculationSourceDescriptor::CreateQueryable(ibValue** /*paParams*/, long /*lSizeArray*/)
{
	return &m_queryable;
}

void ibRecalculationSourceDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	if (m_meta == nullptr)
		return;
	// The record being recalculated + the calculation type, then the recalculation's own dimensions.
	explorer.AppendColumn(m_queryable.RecalculationObjectColumn(), /*enabled*/ true, /*visible*/ true);
	explorer.AppendColumn(m_queryable.CalculationTypeColumn(),     /*enabled*/ true, /*visible*/ true);
	for (const ibValueMetaObjectDimension* dimension : m_meta->GetDimensionArrayObject())
		if (dimension != nullptr)
			explorer.AppendColumn(dimension, /*enabled*/ true, /*visible*/ true);
}

//***********************************************************************
//*                 ibValueMetaObjectRecalculation                      *
//***********************************************************************

ibValueMetaObjectRecalculation::ibValueMetaObjectRecalculation() : ibValueMetaObjectCompositeData()
{
}

ibValueMetaObjectRecalculation::~ibValueMetaObjectRecalculation()
{
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

// A recalculation carries no properties of its own beyond Name/Synonym (held by the base) and its
// dimension children (their own metaobjects). ReadData/WriteData therefore just chain to the base.
bool ibValueMetaObjectRecalculation::ReadData(const ibDataNode& node)
{
	return ibValueMetaObjectCompositeData::ReadData(node);
}

bool ibValueMetaObjectRecalculation::WriteData(ibDataNode& node) const
{
	return ibValueMetaObjectCompositeData::WriteData(node);
}

//***********************************************************************
//*								Events								    *
//***********************************************************************

bool ibValueMetaObjectRecalculation::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	return ibValueMetaObjectCompositeData::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectRecalculation::OnLoadMetaObject(ibMetaData* metaData)
{
	return ibValueMetaObjectCompositeData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRecalculation::OnSaveMetaObject(int flags)
{
	return ibValueMetaObjectCompositeData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRecalculation::OnDeleteMetaObject()
{
	return ibValueMetaObjectCompositeData::OnDeleteMetaObject();
}

bool ibValueMetaObjectRecalculation::OnReloadMetaObject()
{
	ibValueMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);
	if (metaObject != nullptr && metaObject->OnReloadMetaObject())
		return ibValueMetaObjectCompositeData::OnReloadMetaObject();
	return false;
}

bool ibValueMetaObjectRecalculation::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObjectCompositeData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRecalculation::OnAfterRunMetaObject(int flags)
{
	// Register the recalculation as an L4 query source (parent-qualified "<Register>.<Recalculation>").
	// Register ALWAYS — the factory is PER-CONFIG (in the metadata), so a read-only DB load still
	// registers its OWN sources into its OWN factory or the recalculation can't resolve on that config.
	m_metaData->RegisterSource(&m_queryable);
	return ibValueMetaObjectCompositeData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectRecalculation::OnBeforeCloseMetaObject()   // un-resolve — mirror of OnRun's RegisterSource
{
	m_metaData->UnregisterSource(&m_queryable);
	return ibValueMetaObjectCompositeData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectRecalculation::OnAfterCloseMetaObject()
{
	return ibValueMetaObjectCompositeData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectRecalculation, "Recalculation", g_metaRecalculationCLSID);
