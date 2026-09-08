#ifndef _CALC_REGISTER_MANAGER_H__
#define _CALC_REGISTER_MANAGER_H__

#include "calculationRegister.h"

class ibValueManagerDataObjectCalculationRegister :
	public ibValueManagerDataObject {
	public:

	ibValue Get(const ibValue& cFilter = ibValue());
	ibValue Get(const ibValue& cPeriod, const ibValue& cFilter);

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
