#ifndef _CALC_REGISTER_MANAGER_H__
#define _CALC_REGISTER_MANAGER_H__

#include "calculationRegister.h"

class ibValueManagerDataObjectCalculationRegister :
	public ibValueManagerDataObject {
	public:

	ibValue Get(const ibValue& cFilter = ibValue());
	ibValue Get(const ibValue& cPeriod, const ibValue& cFilter);

	// GetDisplacement(Filter) — reads the filtered action-period records and returns them with their
	// ACTUAL action period (ActualActionPeriodStart/End) computed by the tested displacement kernel
	// (ibComputeActionPeriodDisplacement). Interim priority = read order, exactly as the record-set
	// write hook does; the per-calculation-type priority replaces only that derivation once predefined
	// calculation-type displacing data is imported. Empty table when the register has no action period.
	ibValue GetDisplacement(const ibValue& cFilter = ibValue());

	// GetBase(BaseRegister, Filter) — the proportional-by-period base (ПолучитьБазу). For each filtered
	// record of THIS (dependent) register it sums, per base-register resource, that resource weighted by
	// how much of each base record's ACTUAL action period (after displacement in the base register) lies
	// inside the dependent record's BASE period: value * overlap / total. Base records are matched to the
	// dependent record by shared-name dimension VALUES (the main<->base dimension mapping). Returns a value
	// table = the dependent record's own attributes plus one "Base<Resource>" column per base resource.
	// Uses the tested kernels ibComputeActionPeriodDisplacement + ibComputeBaseContributions.
	ibValue GetBase(const ibValue& cBaseRegister, const ibValue& cFilter = ibValue());

	ibValueManagerDataObjectCalculationRegister(const ibValueMetaObjectCalculationRegister* metaObject = nullptr) : m_metaObject(metaObject) { m_members.Bind(this, &ibValueManagerDataObjectCalculationRegister::FillManagerMethods); }
	virtual ~ibValueManagerDataObjectCalculationRegister() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectCalculationRegister* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray); //method call

protected:
	const ibValueMetaObjectCalculationRegister* m_metaObject;
private:
};

#endif
