////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base manager for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "globalContextManager.h"
#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueJob.h"   // ScheduledJobs.Predefined — the collection, its row and the schedule


#include "backend/objCtor.h"

class ibValueGlobalContextStructureManager : public ibValueStructure {
	public:

	ibValueGlobalContextStructureManager() : m_metaData(nullptr), m_clsid(0) {}
	ibValueGlobalContextStructureManager(const ibClassID& clsid, ibMetaData* metaData)
		: ibValueStructure(true), m_metaData(metaData), m_clsid(clsid) {

		for (const auto object : m_metaData->GetAnyArrayObject(clsid)) {
			ibCtorMetaValueType* so = m_metaData->GetTypeCtor(object, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
			if (so == nullptr)
				continue;
			ibValuePtr<ibValue> createdValue(so->CreateObject());
			// Name surface builds lazily on first GetPMethods() — no eager populate.
			ibValueStructure::Insert(object->GetName(), createdValue);
		}
	}

	virtual wxString GetClassName() const {
		ibCtorAbstractType* so = m_metaData->GetAvailableCtor(m_clsid);
		if (so != nullptr)
			return so->GetClassName() + wxT("Manager");
		return ibValueStructure::GetClassName();
	}

	virtual wxString GetString() const {
		ibCtorAbstractType* so = m_metaData->GetAvailableCtor(m_clsid);
		if (so != nullptr)
			return so->GetClassName() + wxT("Manager");
		return ibValueStructure::GetString();
	}

private:

	ibMetaData* m_metaData;

	ibClassID m_clsid;

};


// ScheduledJobs — ONE door, TWO forks. Both kinds of scheduled job live behind the same name,
// because they are one mechanism and not two that happen to sound alike:
//
//   ScheduledJobs.Predefined.…      — the predefined jobs: no rows, no manager of their own, so
//                                     the fork IS the collection — every one of them, with its
//                                     live settings, walked with For Each.
//   ScheduledJobs.Parameterized.<Name>
//                                   — a parameterized job's manager, the ordinary one a reference
//                                     object gets: Select(), FindByDescription(), Execute(ref).
//
// Two forks rather than "predefined here, job names directly there": a name at the top level
// would put the platform's own member and a configuration's job in one namespace, where the first
// job called Predefined shadows the way to reach every other one. Naming both sides also says
// which kind one is looking at, which is the question a reader of the script actually has.
class ibValueScheduledJobsManager : public ibValueStructure {
public:
	ibValueScheduledJobsManager(ibMetaData* metaData)
		: ibValueStructure(true)   // read-only: the forks are the platform's, not a caller's map
	{
		ibValuePtr<ibValuePredefinedJobs> predefined(new ibValuePredefinedJobs(metaData));
		ibValueStructure::Insert(wxT("Predefined"), predefined);

		// The parameterized fork is the SAME parameterised manager every other metatype gets —
		// handed this metatype's clsid. Nothing about jobs is special on this side, and that is
		// the point: a job's rows select exactly like a catalog's.
		ibValuePtr<ibValueGlobalContextStructureManager> parameterized(
			new ibValueGlobalContextStructureManager(g_metaParameterizedJobCLSID, metaData));
		ibValueStructure::Insert(wxT("Parameterized"), parameterized);
	}

	virtual wxString GetClassName() const { return wxT("ScheduledJobsManager"); }
	virtual wxString GetString() const { return wxT("ScheduledJobsManager"); }
};

enum
{
	enConstants = 0,
	enCatalogs,
	enDocuments,
	enEnumerations,
	enDataProcessors,
	enExternalDataProcessors,
	enReports,
	enExternalReports,
	enInformationRegisters,
	enAccumulationRegisters,
	enChartsOfCharacteristicTypes,
	enChartsOfAccounts,
	enAccountingRegisters,
	enScheduledJobs,
	enSessionParameters,
	// OES-RU: Russian aliases for the global collection namespaces (1C-style module code:
	// Перечисления.X.Value, Справочники.Y, Документы.Z …). Same managers as the English props.
	enRuConstants,
	enRuCatalogs,
	enRuDocuments,
	enRuEnumerations,
	enRuDataProcessors,
	enRuReports,
	enRuInformationRegisters,
	enRuAccumulationRegisters,
	enRuChartsOfCharacteristicTypes,
	enRuChartsOfAccounts,
	enRuAccountingRegisters,
	enRuSessionParameters
};

void ibValueGlobalContextManager::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Constants"));
	helper.AppendProp(wxT("Catalogs"));
	helper.AppendProp(wxT("Documents"));
	helper.AppendProp(wxT("Enumerations"));
	helper.AppendProp(wxT("DataProcessors"));
	helper.AppendProp(wxT("ExternalDataProcessors"));
	helper.AppendProp(wxT("Reports"));
	helper.AppendProp(wxT("ExternalReports"));
	helper.AppendProp(wxT("InformationRegisters"));
	helper.AppendProp(wxT("AccumulationRegisters"));
	helper.AppendProp(wxT("ChartsOfCharacteristicTypes"));
	helper.AppendProp(wxT("ChartsOfAccounts"));
	helper.AppendProp(wxT("AccountingRegisters"));
	// ONE entry point for scheduled work, two forks inside it — Predefined and Parameterized. See
	// ibValueScheduledJobsManager above for why both live behind one name.
	helper.AppendProp(wxT("ScheduledJobs"));
	// The configuration's own declared parameters of this session. Read anywhere,
	// written only by the session module (metaSessionParameterObject.h).
	helper.AppendProp(wxT("SessionParameters"));

	// OES-RU: Russian aliases (order must match the enRu* enum block). UTF-8 byte escapes — no /utf-8.
	helper.AppendProp(wxString::FromUTF8("\xD0\x9A\xD0\xBE\xD0\xBD\xD1\x81\xD1\x82\xD0\xB0\xD0\xBD\xD1\x82\xD1\x8B"));                                         // Константы
	helper.AppendProp(wxString::FromUTF8("\xD0\xA1\xD0\xBF\xD1\x80\xD0\xB0\xD0\xB2\xD0\xBE\xD1\x87\xD0\xBD\xD0\xB8\xD0\xBA\xD0\xB8"));                         // Справочники
	helper.AppendProp(wxString::FromUTF8("\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82\xD1\x8B"));                                         // Документы
	helper.AppendProp(wxString::FromUTF8("\xD0\x9F\xD0\xB5\xD1\x80\xD0\xB5\xD1\x87\xD0\xB8\xD1\x81\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F"));                 // Перечисления
	helper.AppendProp(wxString::FromUTF8("\xD0\x9E\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB1\xD0\xBE\xD1\x82\xD0\xBA\xD0\xB8"));                                         // Обработки
	helper.AppendProp(wxString::FromUTF8("\xD0\x9E\xD1\x82\xD1\x87\xD0\xB5\xD1\x82\xD1\x8B"));                                                                 // Отчеты
	helper.AppendProp(wxString::FromUTF8("\xD0\xA0\xD0\xB5\xD0\xB3\xD0\xB8\xD1\x81\xD1\x82\xD1\x80\xD1\x8B\xD0\xA1\xD0\xB2\xD0\xB5\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB9"));                 // РегистрыСведений
	helper.AppendProp(wxString::FromUTF8("\xD0\xA0\xD0\xB5\xD0\xB3\xD0\xB8\xD1\x81\xD1\x82\xD1\x80\xD1\x8B\xD0\x9D\xD0\xB0\xD0\xBA\xD0\xBE\xD0\xBF\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F"));   // РегистрыНакопления
	helper.AppendProp(wxString::FromUTF8("\xD0\x9F\xD0\xBB\xD0\xB0\xD0\xBD\xD1\x8B\xD0\x92\xD0\xB8\xD0\xB4\xD0\xBE\xD0\xB2\xD0\xA5\xD0\xB0\xD1\x80\xD0\xB0\xD0\xBA\xD1\x82\xD0\xB5\xD1\x80\xD0\xB8\xD1\x81\xD1\x82\xD0\xB8\xD0\xBA")); // ПланыВидовХарактеристик
	helper.AppendProp(wxString::FromUTF8("\xD0\x9F\xD0\xBB\xD0\xB0\xD0\xBD\xD1\x8B\xD0\xA1\xD1\x87\xD0\xB5\xD1\x82\xD0\xBE\xD0\xB2"));                         // ПланыСчетов
	helper.AppendProp(wxString::FromUTF8("\xD0\xA0\xD0\xB5\xD0\xB3\xD0\xB8\xD1\x81\xD1\x82\xD1\x80\xD1\x8B\xD0\x91\xD1\x83\xD1\x85\xD0\xB3\xD0\xB0\xD0\xBB\xD1\x82\xD0\xB5\xD1\x80\xD0\xB8\xD0\xB8")); // РегистрыБухгалтерии
	helper.AppendProp(wxString::FromUTF8("\xD0\x9F\xD0\xB0\xD1\x80\xD0\xB0\xD0\xBC\xD0\xB5\xD1\x82\xD1\x80\xD1\x8B\xD0\xA1\xD0\xB5\xD0\xB0\xD0\xBD\xD1\x81\xD0\xB0")); // ПараметрыСеанса
}

#include "backend/metaCollection/metaSessionParameterObject.h"   // the metatype AND the value it yields
#include "backend/metaCollection/partial/dataProcessorManager.h"
#include "backend/metaCollection/partial/dataReportManager.h"

bool ibValueGlobalContextManager::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enConstants:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaConstantCLSID, m_metaData);
		return true;
	case enCatalogs:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaCatalogCLSID, m_metaData);
		return true;
	case enDocuments:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaDocumentCLSID, m_metaData);
		return true;
	case enEnumerations:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaEnumerationCLSID, m_metaData);
		return true;
	case enDataProcessors:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaDataProcessorCLSID, m_metaData);
		return true;
	case enExternalDataProcessors:
		pvarPropVal = new ibValueManagerDataObjectExternalDataProcessor();
		return true;
	case enReports:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaReportCLSID, m_metaData);
		return true;
	case enExternalReports:
		pvarPropVal = new ibValueManagerDataObjectExternalReport();
		return true;
	case enInformationRegisters:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaInformationRegisterCLSID, m_metaData);
		return true;
	case enAccumulationRegisters:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaAccumulationRegisterCLSID, m_metaData);
		return true;
	case enChartsOfCharacteristicTypes:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaChartOfCharacteristicTypesCLSID, m_metaData);
		return true;
	case enChartsOfAccounts:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaChartOfAccountsCLSID, m_metaData);
		return true;
	case enAccountingRegisters:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaAccountingRegisterCLSID, m_metaData);
		return true;
	case enScheduledJobs:
		pvarPropVal = new ibValueScheduledJobsManager(m_metaData);
		return true;
	case enSessionParameters:
		pvarPropVal = new ibValueSessionParameters(m_metaData);
		return true;

	// OES-RU: Russian-alias props resolve to the SAME managers as their English counterparts.
	case enRuConstants:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaConstantCLSID, m_metaData);
		return true;
	case enRuCatalogs:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaCatalogCLSID, m_metaData);
		return true;
	case enRuDocuments:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaDocumentCLSID, m_metaData);
		return true;
	case enRuEnumerations:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaEnumerationCLSID, m_metaData);
		return true;
	case enRuDataProcessors:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaDataProcessorCLSID, m_metaData);
		return true;
	case enRuReports:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaReportCLSID, m_metaData);
		return true;
	case enRuInformationRegisters:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaInformationRegisterCLSID, m_metaData);
		return true;
	case enRuAccumulationRegisters:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaAccumulationRegisterCLSID, m_metaData);
		return true;
	case enRuChartsOfCharacteristicTypes:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaChartOfCharacteristicTypesCLSID, m_metaData);
		return true;
	case enRuChartsOfAccounts:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaChartOfAccountsCLSID, m_metaData);
		return true;
	case enRuAccountingRegisters:
		pvarPropVal = new ibValueGlobalContextStructureManager(g_metaAccountingRegisterCLSID, m_metaData);
		return true;
	case enRuSessionParameters:
		pvarPropVal = new ibValueSessionParameters(m_metaData);
		return true;
	}

	return false;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SYSTEM_TYPE_REGISTER(ibValueGlobalContextManager, "GlobalContextManager", system_to_clsid("MG_SYSM"));
SYSTEM_TYPE_REGISTER(ibValueGlobalContextStructureManager, "GlobalContextStructureManager", system_to_clsid("MG_SYAM"));
