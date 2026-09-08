#include "calculationRegister.h"

void ibValueMetaObjectCalculationRegister::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	// ALWAYS subordinate to a recorder — the recorder-family attributes are always active.
	(*m_propertyAttributeLineActive)->ClearFlag(metaDisableFlag);
	(*m_propertyAttributeRecorder)->ClearFlag(metaDisableFlag);
	(*m_propertyAttributeLineNumber)->ClearFlag(metaDisableFlag);
	(*m_propertyAttributePeriod)->ClearFlag(metaDisableFlag);

	ibValueMetaObjectRegisterData::OnPropertyChanged(property, oldValue, newValue);
}
