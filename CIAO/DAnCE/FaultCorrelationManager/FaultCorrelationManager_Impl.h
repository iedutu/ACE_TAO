// -*- C++ -*-

//=============================================================================
/**
 *  @file    FaultCorrelationManager_Impl.h
 *
 *  $Id$
 *
 *  @author Friedhelm Wolf <fwolf@dre.vanderbilt.edu>
 */
//=============================================================================

#ifndef FAULTCORRELATIONMANAGER_IMPL_H_
#define FAULTCORRELATIONMANAGER_IMPL_H_

#include <map>
#include <queue>
#include "ace/Map_Manager.h"
#include "ace/Thread_Mutex.h"
#include "ace/Task.h"
#include "ace/Condition_T.h"
#include "tao/ORB.h"
#include "FaultCorrelationManager_export.h"
#include "Interfaces/FaultCorrelationManagerS.h"
#include "Deployment/Deployment_common.h"
#include "Interfaces/ExecutionManagerDaemonC.h"

namespace DAnCE
{
  /**
   *  @brief Implementation of a manager to provide fail-stop behavior
   *         for component deployments.
   */
  class FAULTCORRELATIONMANAGER_Export FaultCorrelationManager_Impl : 
    public virtual POA_DAnCE::FaultCorrelationManager,
    public ACE_Task_Base
  {
  public:
    // the fixed listener port is caused by the usage of CCM Object locator
    FaultCorrelationManager_Impl (CORBA::ORB_ptr orb,
                                  DAnCE::ExecutionManagerDaemon_ptr exec_mgr,
                                  const PROPERTY_MAP &options);

    virtual ~FaultCorrelationManager_Impl();

    virtual void stop_failover_unit (const char * fou_id);

    // methods from inherited interfaces

    // FaultNotification
    virtual void app_failure (const char * host,
                              const char * application);

    // ExecutionManager
    virtual ::Deployment::DomainApplicationManager_ptr preparePlan (
        const ::Deployment::DeploymentPlan & plan,
        ::Deployment::ResourceCommitmentManager_ptr resourceCommitment);
    
    virtual ::Deployment::DomainApplicationManagers * getManagers (
        void);

    virtual void destroyManager (
        ::Deployment::DomainApplicationManager_ptr manager);

    virtual void shutdown (void);

    virtual int svc (void);

  private:

    // FaultNotification
    void app_failure_i (const char * host,
                        const char * application);

    void process_deployment_plan (const Deployment::DeploymentPlan & plan,
                                  bool deploy = true);

    char * get_property (const char * name,
                         const Deployment::Properties & properties);

  private:
    typedef ACE_Map_Manager<ACE_CString, 
                            Deployment::DomainApplicationManager_var, 
                            ACE_Null_Mutex> TDomainManagers;

    typedef ACE_Map_Manager<ACE_CString,
                            ACE_CString,
                            ACE_Null_Mutex> TStringMap;

    typedef std::map<ACE_CString, ACE_CString> TObjectIdMap;

    typedef ACE_Map_Manager<ACE_CString,
                            TObjectIdMap,
                            ACE_Null_Mutex> TNodeMap;

    typedef TStringMap TInstancesOfPlan;

    struct FailureInfo 
    {
      ACE_CString host;
      ACE_CString application;
    };

  private:
    CORBA::ORB_var orb_;

    DAnCE::ExecutionManagerDaemon_var exec_mgr_;

    PROPERTY_MAP properties_;

    TDomainManagers dams_;

    TInstancesOfPlan instances_;

    TNodeMap nodes_;

    // these parts belong the role as active object
    bool stop_;

    ACE_Thread_Mutex app_failure_lock_;

    ACE_Condition <ACE_Thread_Mutex> new_notification_;

    std::queue <FailureInfo> notification_queue_;
  };
}

#endif /* FAULTCORRELATIONMANAGER_IMPL_H_ */
