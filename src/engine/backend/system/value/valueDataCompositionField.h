#ifndef __VALUE_DATA_COMPOSITION_FIELD_H__
#define __VALUE_DATA_COMPOSITION_FIELD_H__

#include "backend/compiler/value.h"

void ibValueDataCompositionField_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// A DATA-COMPOSITION FIELD IS A FIELD ADDRESSED BY ITS PATH.
//
// It is the lightweight value the composition (СКД) settings model uses to say "this field" — a
// selected field, a filter's left-hand side, an order or grouping item all hold one. It carries no
// data, only the dotted path (e.g. "Номенклатура.Артикул"); comparison and serialization are by that
// path. Constructed as `Новый ПолеКомпоновкиДанных(ИмяПоля)`. The parallel value is
// ПараметрКомпоновкиДанных (a parameter addressed by name); this is the field twin.
class BACKEND_API ibValueDataCompositionField : public ibValueStaticMembers<&ibValueDataCompositionField_BindNames>
{
public:

	wxString m_field;   // the dotted field path — the whole identity of the value

public:

	ibValueDataCompositionField();
	explicit ibValueDataCompositionField(const wxString& field);
	virtual ~ibValueDataCompositionField() {}

	// DataCompositionField(fieldName) — the field path is the single required argument.
	virtual bool Init(ibValue** paParams, const long lSizeArray);

	virtual wxString GetString() const { return m_field; }
	virtual bool IsEmpty() const { return m_field.IsEmpty(); }

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	// Identity is the path — two fields are equal iff their paths are.
	virtual bool CompareValueEQ(const ibValue& cParam) const override;
	virtual bool CompareValueNE(const ibValue& cParam) const override;
};

#endif // __VALUE_DATA_COMPOSITION_FIELD_H__
