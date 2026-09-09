#ifndef __RECALCULATION_H__
#define __RECALCULATION_H__

////////////////////////////////////////////////////////////////////////////
//	Description : Recalculation (Перерасчёт) — a SUBORDINATE metaobject of a
//	              calculation register. It holds a set of DIMENSION children and
//	              contributes ONE physical DB table keyed by those dimensions plus
//	              a reference to the record being recalculated. Shaped after the
//	              DB-backed tabular section (ibValueMetaObjectTableDataRef): it
//	              vends an L4 queryable + a parent-qualified physical table and
//	              registers itself as a query source on run.
////////////////////////////////////////////////////////////////////////////

#include "backend/metaCollection/metaObjectComposite.h"
#include "backend/metaCollection/dimension/metaDimensionObject.h"   // ibValueMetaObjectDimension — the dimension child
#include "backend/query/queryable.h"
#include "backend/query/queryableFactory.h"   // ibQueryableSourceDescriptor — the L4 source descriptor
#include "backend/query/queryColumn.h"         // ibRawDBColumn — synthetic reference columns

#include <memory>

class ibValueMetaObjectRecalculation;

// ibRecalculationQueryable — the L3 queryable for a recalculation's physical table. It navigates over
// the recalculation metaobject: the columns are the dimension children plus two synthetic reference
// columns (the recalculation object and the calculation type). The table is parent-qualified.
class BACKEND_API ibRecalculationQueryable : public ibBackendQueryable {
public:
	explicit ibRecalculationQueryable(const ibValueMetaObjectRecalculation* meta) : m_meta(meta) {}
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override;
	virtual wxString GetQueryTableName() const override;
	virtual ibGuid GetQueryTableGuid() const override;
	virtual wxString GetQueryName() const override;
	virtual ibMetaID GetQueryTableId() const override;
	virtual const ibMetaData* GetMetaData() const override;
	virtual std::vector<ibQuerySortItem> GetIdentitySort() const override;

	// The synthetic reference columns — built on demand, kept because a queryable hands out column
	// POINTERS (a temporary would leave every caller holding a dangling one).
	const ibBackendQueryColumn* RecalculationObjectColumn() const;
	const ibBackendQueryColumn* CalculationTypeColumn() const;

private:
	const ibValueMetaObjectRecalculation*   m_meta;
	mutable std::unique_ptr<ibRawDBColumn>  m_recalcObject;
	mutable std::unique_ptr<ibRawDBColumn>  m_calcType;
};

// ibRecalculationSourceDescriptor — the recalculation's L4 source descriptor. It CONTAINS the vended
// queryable and is parent-qualified: ns = the parent calculation register's kind, name =
// "<Register>.<Recalculation>", reached as the 3-segment source. Methods are out-of-line (the parent
// type is incomplete here).
class BACKEND_API ibRecalculationSourceDescriptor : public ibQueryableSourceDescriptor {
public:
	explicit ibRecalculationSourceDescriptor(ibValueMetaObjectRecalculation* meta);
	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
	const ibBackendQueryable* GetQueryable() const { return &m_queryable; }
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
private:
	ibValueMetaObjectRecalculation* m_meta;
	ibRecalculationQueryable        m_queryable;
};

// ibValueMetaObjectRecalculation — the metaobject. A subordinate of a calculation register that holds
// dimension children and vends a single physical table.
class BACKEND_API ibValueMetaObjectRecalculation : public ibValueMetaObjectCompositeData, public ibBackendQueryableHolder {
public:

	ibValueMetaObjectRecalculation();
	virtual ~ibValueMetaObjectRecalculation();

	//support icons
	virtual wxIcon GetIcon() const override;
	static wxIcon GetIconGroup();

	// A recalculation accepts DIMENSION children only.
	virtual ibClassID ResolveChild(const ibClassID& clsid) const override {
		if (clsid == g_metaDimensionCLSID)
			return clsid;
		return 0;
	}

	//the metaobject VENDS its queryable (via the L4 source descriptor it owns).
	virtual const ibBackendQueryable* GetQueryable() const override { return m_queryable.GetQueryable(); }

	//physical DB table — parent-qualified: "<Register><id>_RC<id>".
	virtual wxString GetPhysicalTableName() const {
		ibValueMetaObject* parentMeta = GetParent();
		wxASSERT(parentMeta);
		return wxString::Format(wxT("%s%i_RC%i"),
			parentMeta->GetClassName(),
			parentMeta->GetMetaID(),
			GetMetaID()
		);
	}

	//the dimension children of this recalculation.
	std::vector<ibValueMetaObjectDimension*> GetDimensionArrayObject() const {
		std::vector<ibValueMetaObjectDimension*> array;
		FillArrayObjectByFilter<ibValueMetaObjectDimension>(array, { g_metaDimensionCLSID });
		return array;
	}

	// The abstract contract of ibValueMetaObjectCompositeData: the generic attribute list. A
	// recalculation's columns are its dimension children (plus the synthetic reference columns, which
	// are vended, not declared attributes) — the schema builder / query generator meet them here.
	using ibValueMetaObjectCompositeData::GetGenericAttributeArrayObject;
	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaDimensionCLSID });
		return array;
	}

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags) override;
	virtual bool OnLoadMetaObject(ibMetaData* metaData) override;
	virtual bool OnSaveMetaObject(int flags) override;
	virtual bool OnDeleteMetaObject() override;

	//for designer
	virtual bool OnReloadMetaObject() override;

	//module manager is started or exit
	virtual bool OnBeforeRunMetaObject(int flags) override;
	virtual bool OnAfterRunMetaObject(int flags) override;
	virtual bool OnBeforeCloseMetaObject() override;
	virtual bool OnAfterCloseMetaObject() override;

protected:

	// Declare the recalculation table (dimension columns + the two synthetic reference columns + the
	// lookup index). It does NOT recurse — its children are dimensions, which are its own columns.
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	// the L4 source descriptor — CONTAINS the vended queryable (stable for this recalculation's life)
	// and is registered with the factory on run / close; GetQueryable() forwards to it.
	ibRecalculationSourceDescriptor m_queryable{ this };

	friend class ibRecalculationQueryable;
	friend class ibRecalculationSourceDescriptor;
	friend class ibMetaData;
};

#endif
