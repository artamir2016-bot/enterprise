#include "calculationRegister.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/system/systemManager.h"
#include "backend/calculation/actionPeriodDisplacement.h"   // ibComputeActionPeriodDisplacement

#include <vector>

////////////////////////////////////////////////////////////////////////////////////////////////////

// WriteRecordSet is overridden below to compute the actual action period before storing. DeleteRecordSet
// stays inherited from ibValueRecordSetObject (Phase B template-method) — the scaffold is in
// commonObject.cpp; the Begin/Commit + LockByKeys helpers it calls live in commonObjectRecordSetQuery.cpp.

// Fill ActualActionPeriodStart/End for every row of the set by displacement over the action periods.
// A record's actual action period is what remains after subtracting the action periods of higher-priority
// records — computed by the tested kernel ibComputeActionPeriodDisplacement. The single [start,end] pair
// stored per record is the bounding span of the remaining sub-intervals (a fully displaced record gets a
// zero-length span at its start).
//
// ⚠ INTERIM PRIORITY = line order: a later record in the set displaces an earlier one. The exact
// per-calculation-type priority (from the chart's displacing lists) arrives once predefined calculation-
// type data is imported; only the priority derivation changes then, not this wiring.
void ibValueRecordSetObjectCalculationRegister::ComputeActualActionPeriod()
{
	const auto* meta = dynamic_cast<const ibValueMetaObjectCalculationRegister*>(GetMetaObject());
	if (meta == nullptr || !meta->IsUseActionPeriod())
		return;

	const ibMetaID apStart  = meta->GetActionPeriodStart()->GetMetaID();
	const ibMetaID apEnd    = meta->GetActionPeriodEnd()->GetMetaID();
	const ibMetaID aapStart = meta->GetActualActionPeriodStart()->GetMetaID();
	const ibMetaID aapEnd   = meta->GetActualActionPeriodEnd()->GetMetaID();

	const long n = GetRowCount();
	if (n == 0)
		return;

	std::vector<ibDataViewItem> items;
	std::vector<ibActionPeriodRecord> recs;
	items.reserve(n);
	recs.reserve(n);
	for (long i = 0; i < n; ++i) {
		const ibDataViewItem item = GetItem(i);
		ibValue s, e;
		GetValueByMetaID(item, apStart, s);
		GetValueByMetaID(item, apEnd, e);
		items.push_back(item);
		recs.push_back({ /*priority*/ (int64_t)i, /*start*/ (int64_t)s.GetDate(), /*end*/ (int64_t)e.GetDate() });
	}

	const std::vector<std::vector<ibActionInterval>> actual = ibComputeActionPeriodDisplacement(recs);
	for (long i = 0; i < n; ++i) {
		int64_t as, ae;
		if (actual[i].empty()) {              // fully displaced -> zero-length span at the original start
			as = recs[i].start;
			ae = recs[i].start;
		} else {
			as = actual[i].front().start;
			ae = actual[i].back().end;
		}
		SetValueByMetaID(items[i], aapStart, ibValue(wxDateTime(wxLongLong(as))));
		SetValueByMetaID(items[i], aapEnd,   ibValue(wxDateTime(wxLongLong(ae))));
	}
}

bool ibValueRecordSetObjectCalculationRegister::WriteRecordSet(bool replace, bool clearTable)
{
	ComputeActualActionPeriod();
	return ibValueRecordSetObject::WriteRecordSet(replace, clearTable);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const ibSourceExplorer* ibValueRecordManagerObjectCalculationRegister::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(
		wxT("Ref"), _("Ref"), m_metaObject->GetMetaID(), GetClassType(),
		false, false
	);

	ibValueMetaObjectCalculationRegister* metaRef = nullptr;

	// A calculation register is always dated (subordinate to a recorder), so the period is always
	// part of its source.
	if (m_metaObject->ConvertToValue(metaRef)) {
		m_sourceExplorer.AppendColumn(metaRef->GetRegisterPeriod());
	}

	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		m_sourceExplorer.AppendColumn(object);
	}

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		m_sourceExplorer.AppendColumn(object);
	}

	for (const auto object : m_metaObject->GetAttributeArrayObject()) {
		m_sourceExplorer.AppendColumn(object);
	}

	return &m_sourceExplorer;
}

#pragma region _form_builder_h_
void ibValueRecordManagerObjectCalculationRegister::ShowFormValue(const wxString& strFormName, ibBackendControlFrame* ownerControl)
{
	ibBackendValueForm* const foundedForm = GetForm();

	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	//if form is not initialized then generate
	ibBackendValueForm* const valueForm =
		GetFormValue(strFormName, ownerControl);

	if (valueForm != nullptr) {
		valueForm->Modify(m_recordSet->IsModified());
		valueForm->ShowForm();
	}
}

ibBackendValueForm* ibValueRecordManagerObjectCalculationRegister::GetFormValue(const wxString& strFormName, ibBackendControlFrame* ownerControl)
{
	ibBackendValueForm* const foundedForm = GetForm();

	if (foundedForm == nullptr) {

		ibBackendValueForm* createdForm = m_metaObject->CreateAndBuildForm(
			strFormName,
			ibValueMetaObjectCalculationRegister::eFormRecord,
			ownerControl,
			this,
			m_objGuid
		);

		if (createdForm != nullptr)
			createdForm->CloseOnOwnerClose(false);

		return createdForm;
	}

	return foundedForm;
}
#pragma endregion

bool ibValueRecordManagerObjectCalculationRegister::WriteRegister(bool replace)
{
	if (!appData->DesignerMode())
	{
		ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();

		if (!scope || !scope->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!ibBackendException::IsEvalMode())
		{
			{
				scope.SafeBeginTransaction();

				bool newObject = ibValueRecordManagerObjectCalculationRegister::IsNewObject();

				// A REGISTER'S KEY FLOATS OVER ITS DIMENSIONS, so editing one does not modify a record — it
				// REPLACES it: the old key is gone from the table and a row under a new key is what remains.
				// SaveData rewrites m_objGuid's composite in place (SetKeyValues), so the previous one has to be
				// taken now, before the write.
				const ibRowMetaValues keyBefore = m_objGuid.GetKeyValues();

				if (!SaveData()) {
					scope.SafeRollBackTransaction();
					ibBackendCoreException::Error(_("Register '%s': failed to store the record"),
						m_metaObject != nullptr ? m_metaObject->GetSynonym() : wxString());
					return false;
				}

				ibBackendValueForm::UpdateFormUniqueKey(m_objGuid);

				scope.SafeCommitTransaction();

				ibBackendValueForm* const valueForm = GetForm();

				// WHICH NEWS THE LIST NEEDS is not "was this new" but "can the list still find the row it knows".
				// It identifies rows by their key, so only an identity that APPEARED or MOVED is worth an anchor;
				// a plain re-write leaves every key where it was and must not drag the user's cursor to it (an
				// object form sits open while the list is browsed elsewhere). A key change is, for the list,
				// exactly a create: the row it held is gone, this one is new.
				const bool keyMoved = !newObject && m_objGuid.GetKeyValues() != keyBefore;

				if ((newObject || keyMoved) && valueForm != nullptr) valueForm->NotifyCreate(GetValue());
				else if (valueForm != nullptr) valueForm->NotifyChange(GetValue());
			}

			m_recordSet->Modify(false);
		}
	}

	return true;
}

bool ibValueRecordManagerObjectCalculationRegister::DeleteRegister()
{
	if (!appData->DesignerMode())
	{
		ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();

		if (!scope || !scope->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!ibBackendException::IsEvalMode())
		{
			{
				ibBackendValueForm* const valueForm = GetForm();
				{
					scope.SafeBeginTransaction();

					if (!DeleteData()) {
						scope.SafeRollBackTransaction();
						ibBackendCoreException::Error(_("Register '%s': failed to delete the record"),
							m_metaObject != nullptr ? m_metaObject->GetSynonym() : wxString());
						return false;
					}

					scope.SafeCommitTransaction();

					if (valueForm != nullptr) valueForm->NotifyDelete(GetValue());
				}
				m_recordSet->Modify(false);
			}
		}
	}

	return true;
}

enum recordManager
{
	enCopyRecordManager,
	enWriteRecordManager,
	enDeleteRecordManager,
	enModifiedRecordManager,
	enReadRecordManager,
	enSelectedRecordManager,
	enGetFormRecord,
	enGetTemplate,
	enGetMetadataRecordManager
};

enum recordSet
{
	enAdd = 0,
	enCount,
	enClear,
	enLoad,
	enUnload,
	enWriteRecordSet,
	enModifiedRecordSet,
	enReadRecordSet,
	enSelectedRecordSet,
	enGetMetadataRecordSet,
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueRecordSetObjectCalculationRegister::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Add"), wxT("Add()"));
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
	helper.AppendFunc(wxT("Write"), 1, wxT("Write(replace : boolean)"));
	helper.AppendFunc(wxT("Load"), 1, wxT("Load(value : any table)"));
	helper.AppendFunc(wxT("Unload"), wxT("Unload()"));
	helper.AppendFunc(wxT("Modified"), wxT("Modified()"));
	helper.AppendFunc(wxT("Read"), wxT("Read()"));
	helper.AppendFunc(wxT("Selected"), wxT("Selected()"));
	helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));

	// ThisObject + Filter are bound in InitializeObject (context / export) —
	// no manual prop AppendProp on the record-set helper.
}

void ibValueRecordManagerObjectCalculationRegister::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Copy"), wxT("Copy()"));
	helper.AppendFunc(wxT("Write"), 1, wxT("Write(replace : boolean)"));
	helper.AppendFunc(wxT("Delete"), wxT("Delete()"));
	helper.AppendFunc(wxT("Modified"), wxT("Modified()"));
	helper.AppendFunc(wxT("Read"), wxT("Read()"));
	helper.AppendFunc(wxT("Selected"), wxT("Selected()"));
	helper.AppendFunc(wxT("GetFormRecord"), 3, wxT("GetFormRecord(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	helper.AppendFunc(wxT("GetMetadata"), wxT("getMetadata()"));

	//set object name
	wxString objectName;

	//fill custom object
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		helper.AppendProp(
			objectName,
			object->GetMetaID()
		);
	}
}

bool ibValueRecordManagerObjectCalculationRegister::SetPropVal(const long lPropNum, const ibValue& varPropVal)       //setting attribute
{
	return SetValueByMetaID(
		m_members.GetPropData(lPropNum), varPropVal
	);
}

bool ibValueRecordManagerObjectCalculationRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return GetValueByMetaID(
		m_members.GetPropData(lPropNum), pvarPropVal
	);
}

//////////////////////////////////////////////////////////////////////////

bool ibValueRecordSetObjectCalculationRegister::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordSetObjectCalculationRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return false;
}

bool ibValueRecordSetObjectCalculationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);

	switch (lMethodNum)
	{
	case recordSet::enAdd:
		pvarRetValue = new ibValueRecordSetObjectRegisterReturnLine(this, GetItem(AppendRow()));
		return true;
	case recordSet::enCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case recordSet::enClear:
		ibValueModelStorage::Clear();
		return true;
	case recordSet::enLoad:
		LoadDataFromTable(paParams[0]->ConvertToType<ibValueModel>());
		return true;
	case recordSet::enUnload:
		pvarRetValue = SaveDataToTable();
		return true;
	case recordSet::enWriteRecordSet:
		WriteRecordSet(
			lSizeArray > 0 ?
			paParams[0]->GetBoolean() : true
		);
		return true;
	case recordSet::enModifiedRecordSet:
		pvarRetValue = m_objModified;
		return true;
	case recordSet::enReadRecordSet:
		Read();
		return true;
	case recordSet::enSelectedRecordSet:
		pvarRetValue = Selected();
		return true;
	case recordSet::enGetMetadataRecordSet:
		pvarRetValue = GetMetaObject();
		return true;
	}

	return false;
}

bool ibValueRecordManagerObjectCalculationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case recordManager::enCopyRecordManager:
		pvarRetValue = CopyRegister();
		return true;
	case recordManager::enWriteRecordManager:
		pvarRetValue = WriteRegister(
			lSizeArray > 0 ?
			paParams[0]->GetBoolean() : true
		);
		return true;
	case recordManager::enDeleteRecordManager:
		pvarRetValue = DeleteRegister();
		return true;
	case recordManager::enModifiedRecordManager:
		pvarRetValue = m_recordSet->IsModified();
		return true;
	case recordManager::enReadRecordManager:
		m_recordSet->Read();
		return true;
	case recordManager::enSelectedRecordManager:
		pvarRetValue = m_recordSet->Selected();
		return true;
	case recordManager::enGetFormRecord:
		pvarRetValue = GetFormValue(
			lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr
		);
		return true;
	case enGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	case recordManager::enGetMetadataRecordManager:
		pvarRetValue = m_metaObject;
		return true;
	}

	return false;
}
