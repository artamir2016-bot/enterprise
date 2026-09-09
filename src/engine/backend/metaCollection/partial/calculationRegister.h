#ifndef __CALCULATION_REGISTER_H__
#define __CALCULATION_REGISTER_H__

#include "commonObject.h"
#include "calculationRegisterEnum.h"
#include "backend/query/queryable.h"
// The register-shared lowering: ibRegFilterPredicate / ibRegFlatLeaves / ibRegCompositeIR — a
// calculation register filters its dimensions by the same rule the other registers do, so the rule
// lives in one shared file.
#include "backend/metaCollection/partial/registerQueryLowering.h"

#include <memory>

class ibValueMetaObjectCalculationRegister;

class ibValueMetaObjectCalculationRegister : public ibValueMetaObjectRegisterData {
	public:
private:
	enum
	{
		eFormRecord = 1,
		eFormList = 2,
	};

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(wxT("FormRecord"), _("Form record"), eFormRecord);
		formList.AppendItem(wxT("FormList"), _("Form list"), eFormList);
		return formList;
	}

	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

public:
	class ibValueMetaObjectRecordManager : public ibValueMetaObject {
	public:
		ibValueMetaObjectRecordManager() : ibValueMetaObject() {}
	};

public:

	ibValueMetaObjectCalculationRegister();
	virtual ~ibValueMetaObjectCalculationRegister();

	// A calculation register additionally owns Recalculation subordinate objects (Перерасчёт) —
	// child tables keyed by (recalc object, dimensions). Everything else (Dimension/Resource/Attribute)
	// stays with the register-data base.
	virtual ibClassID ResolveChild(const ibClassID& clsid) const override {
		if (clsid == g_metaRecalculationCLSID)
			return clsid;
		return ibValueMetaObjectRegisterData::ResolveChild(clsid);
	}

	// The Recalculation subordinate objects of this register (public — the designer navigator lists them).
	std::vector<ibValueMetaObject*> GetRecalculationArrayObject() const {
		std::vector<ibValueMetaObject*> array;
		FillArrayObjectByFilter<ibValueMetaObject>(array, { g_metaRecalculationCLSID });
		return array;
	}

	ibCalculationPeriodicity GetPeriodicity() const {
		return m_propertyPeriodicity->GetValueAsEnum();
	}

	// ⭐ ACTION PERIOD — the semantic heart of a calculation register. When on, a calculation record is
	// not a point event but an INTERVAL [start, end] over which it is in force; records of competing
	// calculation types displace each other over overlapping action periods. When off, the record
	// carries only the registration period (when it was entered). See docs (calculation engine).
	bool IsUseActionPeriod() const { return m_propertyUseActionPeriod->GetValueAsBoolean(); }
	void SetUseActionPeriod(bool v) { m_propertyUseActionPeriod->SetValue(v); }
	ibValueMetaObjectAttributePredefined* GetActionPeriodStart()   const { return m_propertyAttributeActionPeriodStart->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetActionPeriodEnd()     const { return m_propertyAttributeActionPeriodEnd->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetRegistrationPeriod()  const { return m_propertyAttributeRegistrationPeriod->GetMetaObject(); }

	// The ACTUAL action period — what remains of a record's action period after displacement by higher
	// priority records (computed by ibComputeActionPeriodDisplacement at write time). Stored columns so
	// a base sum can read the in-force span directly. Present only when the register uses an action period.
	ibValueMetaObjectAttributePredefined* GetActualActionPeriodStart() const { return m_propertyAttributeActualActionPeriodStart->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetActualActionPeriodEnd()   const { return m_propertyAttributeActualActionPeriodEnd->GetMetaObject(); }

	// ⭐ CALCULATION TYPE (ВидРасчёта) — the standard attribute every calculation record carries: which
	// calculation type of the bound chart this record is. Its priority (from the chart's displacing
	// lists) is what feeds displacement. The register is BOUND to exactly one chart of calculation types;
	// binding sets the CalculationType attribute's type to a reference into that chart.
	ibValueMetaObjectAttributePredefined* GetCalculationType() const { return m_propertyAttributeCalculationType->GetMetaObject(); }
	void SetChartOfCalculationTypes(const ibMetaID& chartMetaID);   // binds the register to a chart of calc types

	// ⭐ BASE PERIOD — the interval whose already-computed results a dependent calculation reads as its
	// base (dependency-by-base-period). When on, the record carries [baseStart, baseEnd] naming the span
	// its base amount is summed over. Independent of the action period.
	bool IsUseBasePeriod() const { return m_propertyUseBasePeriod->GetValueAsBoolean(); }
	void SetUseBasePeriod(bool v) { m_propertyUseBasePeriod->SetValue(v); }
	ibValueMetaObjectAttributePredefined* GetBasePeriodStart() const { return m_propertyAttributeBasePeriodStart->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetBasePeriodEnd()   const { return m_propertyAttributeBasePeriodEnd->GetMetaObject(); }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//for designer
	virtual bool OnReloadMetaObject();

	//module manager is started or exit
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//form events
	virtual void OnCreateFormObject(ibValueMetaObjectFormBase* metaForm);
	virtual void OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm);

	//has record manager — a calculation register is always subordinate to a recorder, so it is
	//written as a record SET, never as an independent record.
	virtual bool HasRecordManager() const { return false; }

	//has recorder and period — ALWAYS. A calculation register is by definition subordinate to a
	//recorder and its rows are dated.
	virtual bool HasPeriod() const { return true; }
	virtual bool HasRecorder() const { return true; }

	//get module object in compose object
	virtual const ibValueMetaObjectModule* GetObjectModule() const { return m_propertyObjectModule->GetMetaObject(); }
	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const { return m_propertyManagerModule->GetMetaObject(); }

	//create associate value
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const;

#pragma region _form_builder_h_
	//support form
	virtual ibBackendValueForm* GetRecordForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullUniqueKey) const;
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullUniqueKey) const;
#pragma endregion

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

	/**
	* Property events
	*/
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	// Additive contract — a calculation register is ALWAYS subordinate to a recorder, so the
	// recorder-family predefined attributes (active flag, period, recorder, line number) are always
	// part of it, with no WriteMode gate.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRegisterData::FillArrayObjectByPredefinedAttribute(array);

		array.emplace_back(m_propertyAttributeLineActive->GetMetaObject());
		array.emplace_back(m_propertyAttributePeriod->GetMetaObject());
		array.emplace_back(m_propertyAttributeRecorder->GetMetaObject());
		array.emplace_back(m_propertyAttributeLineNumber->GetMetaObject());
		array.emplace_back(m_propertyAttributeCalculationType->GetMetaObject());   // ВидРасчёта — always present

		// Action-period standard attributes become columns only when the register uses an action
		// period — otherwise a calculation record is a point event and these would be dead columns.
		if (m_propertyUseActionPeriod->GetValueAsBoolean()) {
			array.emplace_back(m_propertyAttributeActionPeriodStart->GetMetaObject());
			array.emplace_back(m_propertyAttributeActionPeriodEnd->GetMetaObject());
			array.emplace_back(m_propertyAttributeRegistrationPeriod->GetMetaObject());
			array.emplace_back(m_propertyAttributeActualActionPeriodStart->GetMetaObject());
			array.emplace_back(m_propertyAttributeActualActionPeriodEnd->GetMetaObject());
		}

		if (m_propertyUseBasePeriod->GetValueAsBoolean()) {
			array.emplace_back(m_propertyAttributeBasePeriodStart->GetMetaObject());
			array.emplace_back(m_propertyAttributeBasePeriodEnd->GetMetaObject());
		}

		return true;
	}

	//get dimension keys — always keyed by the recorder (subordinate register)
	virtual bool FillArrayObjectByDimension(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = { m_propertyAttributeRecorder->GetMetaObject() };
		return true;
	}

	//create manager
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() const;

	//create record set
	virtual ibValueRecordSetObject* CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) const;
	virtual ibValueRecordManagerObject* CreateRecordManagerObjectRegValue(const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) const;

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const;

	//get command section
	virtual ibInterfaceCommandSection GetCommandSection() const { return ibInterfaceCommandSection::ibInterfaceCommandSection_Combined; }

	//load & save metaData from DB

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

protected:

	//get default form
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) const {

		if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_Create)
			return GetRecordForm();
		else if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_List)
			return GetListForm();

		return GetListForm();
	}

private:

	bool FillFormRecord(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormRecord == object->GetTypeForm()) {
				prop->AppendItem(
					object->GetName(),
					object->GetMetaID(),
					object->GetIcon(),
					object);
			}
		}
		return true;
	}

	bool FillFormList(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormList == object->GetTypeForm()) {
				prop->AppendItem(
					object->GetName(),
					object->GetMetaID(),
					object->GetIcon(),
					object);
			}
		}
		return true;
	}

	ibValueMetaObjectRecordManager* m_metaRecordManager;

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyObjectModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("RecordSetModule"), _("Record set module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormRecord = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormRecord"), _("Default Record Form"), &ibValueMetaObjectCalculationRegister::FillFormRecord);
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectCalculationRegister::FillFormList);

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyEnum<ibValueEnumCalculationPeriodicity>* m_propertyPeriodicity = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumCalculationPeriodicity>>(m_categoryData, wxT("Periodicity"), _("Periodicity"), ibCalculationPeriodicity::eCalcWithinDay);

	// Action period configuration + its standard attributes (predefined). The attributes exist for the
	// life of the register (stable metaIDs -> stable fld<metaID> columns), but only enter the schema
	// when UseActionPeriod is on (see FillArrayObjectByPredefinedAttribute).
	ibPropertyBoolean* m_propertyUseActionPeriod = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("UseActionPeriod"), _("Use action period"), false);
	ibPropertyContainer<>* m_propertyAttributeActionPeriodStart  = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("ActionPeriodStart"),  _("Action period start"),  wxEmptyString, ibDateFractions::ibDateFractions_DateTime, true));
	ibPropertyContainer<>* m_propertyAttributeActionPeriodEnd    = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("ActionPeriodEnd"),    _("Action period end"),    wxEmptyString, ibDateFractions::ibDateFractions_DateTime, true));
	ibPropertyContainer<>* m_propertyAttributeRegistrationPeriod = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("RegistrationPeriod"), _("Registration period"), wxEmptyString, ibDateFractions::ibDateFractions_DateTime, true));
	ibPropertyContainer<>* m_propertyAttributeActualActionPeriodStart = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("ActualActionPeriodStart"), _("Actual action period start"), wxEmptyString, ibDateFractions::ibDateFractions_DateTime, true));
	ibPropertyContainer<>* m_propertyAttributeActualActionPeriodEnd   = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("ActualActionPeriodEnd"),   _("Actual action period end"),   wxEmptyString, ibDateFractions::ibDateFractions_DateTime, true));

	// The calculation-type standard attribute — an empty-typed reference whose type is set to the bound
	// chart of calculation types by SetChartOfCalculationTypes (like the recorder's type is set by its
	// posting documents). Always a predefined attribute of a calculation register.
	ibPropertyContainer<>* m_propertyAttributeCalculationType = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateEmptyType(wxT("CalculationType"), _("Calculation type"), wxEmptyString));

	ibPropertyBoolean* m_propertyUseBasePeriod = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("UseBasePeriod"), _("Use base period"), false);
	ibPropertyContainer<>* m_propertyAttributeBasePeriodStart = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("BasePeriodStart"), _("Base period start"), wxEmptyString, ibDateFractions::ibDateFractions_DateTime, true));
	ibPropertyContainer<>* m_propertyAttributeBasePeriodEnd   = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("BasePeriodEnd"),   _("Base period end"),   wxEmptyString, ibDateFractions::ibDateFractions_DateTime, true));

	friend class ibValueRecordSetObjectCalculationRegister;
	friend class ibValueRecordManagerObjectCalculationRegister;

	friend class ibMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordSetObjectCalculationRegister : public ibValueRecordSetObject {
	public:
	ibValueRecordSetObjectCalculationRegister(const ibValueMetaObjectCalculationRegister* metaObject, const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) :
		ibValueRecordSetObject(metaObject, uniqueKey) {
		m_members.Bind(this, &ibValueRecordSetObjectCalculationRegister::FillMembers);
	}
	ibValueRecordSetObjectCalculationRegister(const ibValueRecordSetObjectCalculationRegister& source) :
		ibValueRecordSetObject(source) {
		m_members.Bind(this, &ibValueRecordSetObjectCalculationRegister::FillMembers);
	}
public:

	//default methods
	virtual ibValueRecordSetObject* CopyRegisterValue() {
		return new ibValueRecordSetObjectCalculationRegister(*this);
	}

	// WriteRecordSet is OVERRIDDEN: before the base stores the rows, the actual action period of each
	// record is computed from the set by displacement (ibComputeActionPeriodDisplacement) and written
	// into the ActualActionPeriod columns. DeleteRecordSet stays inherited.
	virtual bool WriteRecordSet(bool replace = true, bool clearTable = true) override;

private:
	// Fill ActualActionPeriodStart/End for every row by displacement over the in-memory set. No-op when
	// the register does not use an action period. INTERIM priority = line order (a later record displaces
	// an earlier one); the exact per-calculation-type priority arrives with imported displacing data.
	void ComputeActualActionPeriod();

public:

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	void FillMembers(ibMemberTable& helper) const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

protected:
	friend class ibValue;
	friend class ibValueMetaObjectCalculationRegister;
};

class ibValueRecordManagerObjectCalculationRegister : public ibValueRecordManagerObject {
	public:
	ibValueRecordManagerObjectCalculationRegister(const ibValueMetaObjectCalculationRegister* metaObject, const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) :
		ibValueRecordManagerObject(metaObject, uniqueKey)
	{
		m_members.Bind(this, &ibValueRecordManagerObjectCalculationRegister::FillMembers);
	}
	ibValueRecordManagerObjectCalculationRegister(const ibValueRecordManagerObjectCalculationRegister& source) :
		ibValueRecordManagerObject(source)
	{
		m_members.Bind(this, &ibValueRecordManagerObjectCalculationRegister::FillMembers);
	}
	virtual ibValueRecordManagerObject* CopyRegister(bool showValue = false) {
		ibValueRecordManagerObject* objectRef = CopyRegisterValue();
		if (objectRef != nullptr && showValue)
			objectRef->ShowFormValue();
		return objectRef;
	}
	virtual bool WriteRegister(bool replace = true);
	virtual bool DeleteRegister();

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	void FillMembers(ibMemberTable& helper) const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//support source data
	virtual const ibSourceExplorer* GetSourceExplorer() const;

#pragma region _form_builder_h_
	//support show
	virtual void ShowFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr);
	virtual ibBackendValueForm* GetFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr);
#pragma endregion

	//support actionData
	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType);
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

protected:
	friend class ibValue;
	friend class ibValueMetaObjectCalculationRegister;
};

#endif
