#ifndef _CALCULATION_REGISTER_ENUM_H__
#define _CALCULATION_REGISTER_ENUM_H__

enum ibCalculationPeriodicity {
	eCalcNonPeriodic,
	eCalcWithinSecond,
	eCalcWithinDay,
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class ibValueEnumCalculationPeriodicity : public ibValueEnumeration<ibCalculationPeriodicity> {
	public:
	ibValueEnumCalculationPeriodicity() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibCalculationPeriodicity::eCalcNonPeriodic, wxT("NonPeriodic"), _("Non periodic"));
		AddEnumeration(ibCalculationPeriodicity::eCalcWithinSecond, wxT("WithinSecond"), _("Within second"));
		AddEnumeration(ibCalculationPeriodicity::eCalcWithinDay, wxT("WithinDay"), _("Within day"));
	}
};
#pragma endregion

#endif
