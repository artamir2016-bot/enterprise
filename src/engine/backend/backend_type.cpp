#include "backend_type.h"
#include "backend/compiler/enumUnit.h"
#include "backend/system/value/valueTable.h"   // g_valueTableCLSID (the _table default); the primitive value clsids come via value.h

// OES-RU: 1C type name → OES ctor name. See declaration in backend_type.h. UTF-8 byte escapes — no /utf-8.
BACKEND_API wxString ibTranslateRuTypeName(const wxString& name)
{
	struct Pair { const char* ru; const char* oes; };
	// Reference kinds: match the LEADING prefix, keep the object name after the dot verbatim.
	static const Pair kPrefix[] = {
		{ "\xD0\xA1\xD0\xBF\xD1\x80\xD0\xB0\xD0\xB2\xD0\xBE\xD1\x87\xD0\xBD\xD0\xB8\xD0\xBA\xD0\xA1\xD1\x81\xD1\x8B\xD0\xBB\xD0\xBA\xD0\xB0.", "CatalogRef." },                                                 // СправочникСсылка.
		{ "\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82\xD0\xA1\xD1\x81\xD1\x8B\xD0\xBB\xD0\xBA\xD0\xB0.", "DocumentRef." },                                                             // ДокументСсылка.
		{ "\xD0\x9F\xD0\xB5\xD1\x80\xD0\xB5\xD1\x87\xD0\xB8\xD1\x81\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5\xD0\xA1\xD1\x81\xD1\x8B\xD0\xBB\xD0\xBA\xD0\xB0.", "EnumerationRef." },                         // ПеречислениеСсылка.
		{ "\xD0\x9F\xD0\xBB\xD0\xB0\xD0\xBD\xD0\x92\xD0\xB8\xD0\xB4\xD0\xBE\xD0\xB2\xD0\xA5\xD0\xB0\xD1\x80\xD0\xB0\xD0\xBA\xD1\x82\xD0\xB5\xD1\x80\xD0\xB8\xD1\x81\xD1\x82\xD0\xB8\xD0\xBA\xD0\xA1\xD1\x81\xD1\x8B\xD0\xBB\xD0\xBA\xD0\xB0.", "ChartOfCharacteristicTypesRef." }, // ПланВидовХарактеристикСсылка.
		{ "\xD0\x9F\xD0\xBB\xD0\xB0\xD0\xBD\xD0\xA1\xD1\x87\xD0\xB5\xD1\x82\xD0\xBE\xD0\xB2\xD0\xA1\xD1\x81\xD1\x8B\xD0\xBB\xD0\xBA\xD0\xB0.", "ChartOfAccountsRef." },                                       // ПланСчетовСсылка.
	};
	for (const Pair& p : kPrefix) {
		const wxString ru = wxString::FromUTF8(p.ru);
		if (name.StartsWith(ru))
			return wxString::FromAscii(p.oes) + name.Mid(ru.length());
	}
	// Built-in creatable types: whole-name match.
	static const Pair kType[] = {
		{ "\xD0\x9C\xD0\xB0\xD1\x81\xD1\x81\xD0\xB8\xD0\xB2", "Array" },                                                                             // Массив
		{ "\xD0\xA1\xD1\x82\xD1\x80\xD1\x83\xD0\xBA\xD1\x82\xD1\x83\xD1\x80\xD0\xB0", "Structure" },                                                 // Структура
		{ "\xD0\xA1\xD0\xBE\xD0\xBE\xD1\x82\xD0\xB2\xD0\xB5\xD1\x82\xD1\x81\xD1\x82\xD0\xB2\xD0\xB8\xD0\xB5", "Container" },                          // Соответствие
		{ "\xD0\xA2\xD0\xB0\xD0\xB1\xD0\xBB\xD0\xB8\xD1\x86\xD0\xB0\xD0\x97\xD0\xBD\xD0\xB0\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB9", "Table" },      // ТаблицаЗначений
		{ "\xD0\x97\xD0\xB0\xD0\xBF\xD1\x80\xD0\xBE\xD1\x81", "Query" },                                                                            // Запрос
		{ "\xD0\xA2\xD0\xB0\xD0\xB1\xD0\xBB\xD0\xB8\xD1\x87\xD0\xBD\xD1\x8B\xD0\xB9\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82", "SpreadsheetDocument" },                              // ТабличныйДокумент
		{ "\xD0\x9E\xD0\xBF\xD0\xB8\xD1\x81\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5\xD0\xA2\xD0\xB8\xD0\xBF\xD0\xBE\xD0\xB2", "TypeDescription" },            // ОписаниеТипов
		{ "\xD0\x9C\xD0\xBE\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82\xD0\x92\xD1\x80\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xB8", "PointInTime" },                // МоментВремени
		{ "\xD0\x93\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB8\xD1\x86\xD0\xB0", "Boundary" },                                                                  // Граница
	};
	for (const Pair& p : kType) {
		if (name == wxString::FromUTF8(p.ru))
			return wxString::FromAscii(p.oes);
	}
	return name;
}

//***********************************************************************
//*                         Type factory                                *
//***********************************************************************


ibValue ibBackendTypeFactory::CreateValue() const
{
	ibValue* refData = CreateValueRef();
	return refData ?
		refData : ibValue();
}

ibValue* ibBackendTypeFactory::CreateValueRef() const
{
	const ibTypeDescription& typeDesc = GetTypeDesc();
	if (typeDesc.GetClsidCount() == 1) {
		const ibClassID& clsid = typeDesc.GetFirstClsid();
		if (ibValue::IsRegisterCtor(clsid)) {
			const ibCtorAbstractType* so = ibValue::GetAvailableCtor(clsid);
			if (so->GetObjectTypeCtor() == ibCtorObjectType::ibCtorObjectType_object_enum) {
				try {
					std::shared_ptr<ibValueEnumerationWrapper> enumVal(
						ibValue::CreateAndConvertObjectRef<ibValueEnumerationWrapper>(so->GetClassName())
					);
					return enumVal->GetEnumVariantValue();
				}
				catch (...) {
				}
				return nullptr;
			}
			try {
				return ibValue::CreateObjectRef(so->GetClassType());
			}
			catch (...) {
				return nullptr;
			}
		}
	}
	return nullptr;
}

#include "backend/system/value/valueType.h"

ibValue ibBackendTypeFactory::AdjustValue() const
{
	return ibValueTypeDescription::AdjustValue(GetTypeDesc());
}

ibValue ibBackendTypeFactory::AdjustValue(const ibValue& varValue) const
{
	return ibValueTypeDescription::AdjustValue(GetTypeDesc(), varValue);
}

ibValue ibBackendTypeFactory::AdjustValue(const ibValue& varValue, const ibTypeDescription& limit) const
{
	return ibValueTypeDescription::AdjustValue(limit, varValue);
}

/////////////////////////////////////////////////////////////////////////////////////

ibValue ibBackendTypeConfigFactory::CreateValue() const
{
	ibValue* refData = CreateValueRef();
	if (refData == nullptr)
		return ibValue();
	return refData;
}

#include "backend/metaData.h"
#include "backend/objCtor.h"

ibValue* ibBackendTypeConfigFactory::CreateValueRef() const
{
	ibMetaData const* metaData = GetMetaData();
	wxASSERT(metaData);
	const ibTypeDescription& typeDesc = GetTypeDesc();
	if (typeDesc.GetClsidCount() == 1) {
		const ibCtorMetaValueType* so = metaData->GetTypeCtor(typeDesc.GetFirstClsid());
		if (so != nullptr) {
			try {
				return metaData->CreateObjectRef(so->GetClassType());
			}
			catch (...) {
				return nullptr;
			}
		}
	}
	return ibBackendTypeFactory::CreateValueRef();
}

ibValue ibBackendTypeConfigFactory::AdjustValue() const
{
	return ibValueTypeDescription::AdjustValue(
		GetTypeDesc(),
		GetMetaData()
	);
}

ibValue ibBackendTypeConfigFactory::AdjustValue(const ibValue& varValue) const
{
	return ibValueTypeDescription::AdjustValue(
		GetTypeDesc(),
		varValue,
		GetMetaData()
	);
}

ibValue ibBackendTypeConfigFactory::AdjustValue(const ibValue& varValue, const ibTypeDescription& limit) const
{
	return ibValueTypeDescription::AdjustValue(
		limit,
		varValue,
		GetMetaData()
	);
}

// The one filter-kind -> default value clsid mapping. Static so both ibVariantDataAttribute::DoSetDefault-
// MetaType and ibValueControl::AutoBindNewSource resolve the SAME default type for a given filter kind.
ibClassID ibBackendTypeConfigFactory::GetDefaultTypeByFilter(ibSelectorDataType filterDataType)
{
	switch (filterDataType) {
	case ibSelectorDataType::ibSelectorDataType_boolean:  return g_valueBooleanCLSID;
	case ibSelectorDataType::ibSelectorDataType_resource: return g_valueNumberCLSID;
	case ibSelectorDataType::ibSelectorDataType_table:    return g_valueTableCLSID;
	case ibSelectorDataType::ibSelectorDataType_reference:
	default:                                              return g_valueStringCLSID;
	}
}

/////////////////////////////////////////////////////////////////////////////////////

#include "backend/query/queryColumn.h"                      // ibBackendSourceColumn — the leaf the dot returns

const ibBackendSourceColumn* ibBackendTypeSourceFactory::WalkSource(
	const ibSourceDescription& desc, bool* valid, wxString* outText) const
{
	if (valid != nullptr) *valid = false;
	const std::vector<ibSourceHop>& path = desc.GetPath();
	if (path.empty()) return nullptr;

	// Gate 1: path[0] must be one of THIS context's source attributes (form-local).
	ibBackendFormAttributeValue* headHolder = FindSourceHolder(desc.GetFirst());
	if (headHolder == nullptr) return nullptr;
	if (outText != nullptr) *outText = headHolder->GetName();
	// A whole-attribute binding (length 1) is valid with no column leaf.
	if (path.size() == 1) { if (valid != nullptr) *valid = true; return nullptr; }

	// Deeper hops delegate to THE shared structure-resolve hop — it walks each source's EXPLORER (the same
	// self-describing structure the runtime value-hop steps), descending into each reference's OWN columns.
	// No metaID -> name -> FindAnyObjectByFilter fallback: the reference-as-source explorer already holds the
	// target's columns, so a miss is a genuinely broken binding. ONE resolve path, the design-time twin of
	// ResolvePath (which the tablebox renderer + GetValueByPath fetch values through).
	ibSourceDataObject* source = headHolder->GetSourceValue();
	const ibBackendSourceColumn* leaf = nullptr;
	const bool resolved = (source != nullptr) && source->WalkColumns(path, 1, leaf, outText);
	if (valid != nullptr) *valid = resolved;
	return resolved ? leaf : nullptr;
}

ibBackendFormAttributeValue* ibBackendTypeSourceFactory::FindSourceHolder(const ibMetaID& id) const
{
	std::vector<ibBackendFormAttributeValue*> holders;
	GetSourceList(holders);
	for (ibBackendFormAttributeValue* holder : holders)
		if (holder != nullptr && id == holder->GetId())
			return holder;
	return nullptr;
}