/*
 *                         OpenSplice DDS
 *
 *   This software and documentation are Copyright 2006 to 2009 PrismTech
 *   Limited and its licensees. All rights reserved. See file:
 *
 *                     $OSPL_HOME/LICENSE
 *
 *   for full copyright notice and license terms.
 *
 */
#include "os.h"
#include "d_groupLocalListener.h"
#include "d_nameSpacesRequestListener.h"
#include "d__groupLocalListener.h"
#include "d__sampleChainListener.h"
#include "d_listener.h"
#include "d_admin.h"
#include "d_eventListener.h"
#include "d_configuration.h"
#include "d_durability.h"
#include "d_group.h"
#include "d_nameSpace.h"
#include "d_newGroup.h"
#include "d_deleteData.h"
#include "d_message.h"
#include "d_sampleRequest.h"
#include "d_groupsRequest.h"
#include "d_deleteData.h"
#include "d_sampleChainListener.h"
#include "d_actionQueue.h"
#include "d_fellow.h"
#include "d_networkAddress.h"
#include "d_publisher.h"
#include "d_table.h"
#include "d_misc.h"
#include "d_waitset.h"
#include "d_store.h"
#include "d_readerRequest.h"
#include "u_group.h"
#include "u_waitsetEvent.h"
#include "v_event.h"
#include "v_service.h"
#include "v_participant.h"
#include "v_entity.h"
#include "v_topic.h"
#include "v_partition.h"
#include "v_group.h"
#include "v_time.h"
#include "c_time.h"
#include "c_iterator.h"
#include "os_heap.h"
#include "os_report.h"

/**
 * TODO: d_groupLocalListenerHandleAlignment: take appropriate action on
 * lazy alignment where injection of data fails.
 */

static c_bool
checkNameSpaces(
    d_fellow fellow,
    c_voidp args)
{
    c_bool* ready;
    d_communicationState state;

    ready = (c_bool*)args;
    state = d_fellowGetCommunicationState(fellow);

    if(state == D_COMMUNICATION_STATE_UNKNOWN){
        *ready = FALSE;
    } else {
        *ready = TRUE;
    }
    return *ready;
}

d_groupLocalListener
d_groupLocalListenerNew(
    d_subscriber subscriber,
    d_sampleChainListener sampleChainListener)
{
    d_groupLocalListener listener;

    listener = NULL;

    if(subscriber){
        listener = d_groupLocalListener(os_malloc(C_SIZEOF(d_groupLocalListener)));
        d_listener(listener)->kind = D_GROUP_LOCAL_LISTENER;
        listener->sampleChainListener = sampleChainListener;
        d_groupLocalListenerInit(listener, subscriber);
    }
    return listener;
}

void
d_groupLocalListenerDeinit(
    d_object object)
{
    d_groupLocalListener listener;

    assert(d_listenerIsValid(d_listener(object), D_GROUP_LOCAL_LISTENER));

    if(object){
        listener = d_groupLocalListener(object);
        d_groupLocalListenerStop(listener);
        d_actionQueueFree(listener->actionQueue);
        d_actionQueueFree(listener->masterMonitor);
        os_mutexDestroy(&(listener->masterLock));
    }
}

void
d_groupLocalListenerFree(
    d_groupLocalListener listener)
{
    assert(d_listenerIsValid(d_listener(listener), D_GROUP_LOCAL_LISTENER));

    d_listenerFree(d_listener(listener));
}

/*************** START NEW IMPL *************************/

static c_bool
checkFellowGroupsKnown(
    d_fellow fellow,
    c_voidp args)
{
    c_long expected;
    c_ulong actual;
    c_bool* known;
    c_bool requested;

    known = (c_bool*)args;

    requested = d_fellowGetGroupsRequested(fellow);

    if(requested){
        expected = d_fellowGetExpectedGroupCount(fellow);

        if(expected != -1){
            actual = d_fellowGetGroupCount(fellow);

            if(actual >= ((c_ulong)expected)){
                *known = TRUE;
            } else {
                *known = FALSE;
            }
        } else if( (d_fellowGetCommunicationState(fellow) == D_COMMUNICATION_STATE_INCOMPATIBLE_STATE) ||
                   (d_fellowGetCommunicationState(fellow) == D_COMMUNICATION_STATE_INCOMPATIBLE_DATA_MODEL))
        {
            *known = TRUE;
        } else {
            *known = FALSE;
        }
    } else {
        *known = TRUE;
    }
    return *known;

}

struct groupInfo {
    c_iter nameSpaces;
    c_bool iAmAMaster;
    d_groupsRequest request;
    d_publisher publisher;
    d_durability durability;
};

static c_bool
requestGroups(
    d_fellow fellow,
    c_voidp args)
{
    c_bool toRequest;
    d_networkAddress addr, master;
    struct groupInfo *info;
    c_long i;
    d_nameSpace ns;
    c_bool notInitial;

    info = (struct groupInfo*)args;

    if(info->iAmAMaster){
        toRequest = d_fellowSetGroupsRequested(fellow);
        addr = d_fellowGetAddress(fellow);

        if(toRequest){
            d_printTimedEvent(info->durability, D_LEVEL_FINE,
                              D_THREAD_GROUP_LOCAL_LISTENER,
                              "Requesting all groups from fellow '%d'.\n",
                              addr->systemId);
            d_messageSetAddressee(d_message(info->request), addr);
            d_publisherGroupsRequestWrite(info->publisher, info->request, addr);
        } else {
            d_printTimedEvent(info->durability, D_LEVEL_FINEST,
                              D_THREAD_GROUP_LOCAL_LISTENER,
                              "No need to request all groups from fellow '%d' again\n",
                              addr->systemId);
        }
        d_networkAddressFree(addr);
    } else {
        addr = d_fellowGetAddress(fellow);
        toRequest = FALSE;

        for(i=0; i<c_iterLength(info->nameSpaces) && (!toRequest); i++){
            ns = d_nameSpace(c_iterObject(info->nameSpaces, i));
            master = d_nameSpaceGetMaster(ns);

            if(d_networkAddressEquals(master, addr)){
                notInitial = d_nameSpaceIsAlignmentNotInitial(ns);

                if(notInitial){
                    d_printTimedEvent(info->durability, D_LEVEL_FINER,
                          D_THREAD_GROUP_LOCAL_LISTENER,
                          "I am very lazy and will not request groups from master '%d'.\n",
                          addr->systemId);
                    toRequest = FALSE;
                } else {
                    toRequest = TRUE;
                }
            }
            d_networkAddressFree(master);
        }
        if(toRequest){
            toRequest = d_fellowSetGroupsRequested(fellow);

            if(toRequest){
                d_printTimedEvent(info->durability, D_LEVEL_FINE,
                                  D_THREAD_GROUP_LOCAL_LISTENER,
                                  "Requesting all groups from fellow '%d'.\n",
                                  addr->systemId);
                d_messageSetAddressee(d_message(info->request), addr);
                d_publisherGroupsRequestWrite(info->publisher, info->request, addr);
            } else {
                d_printTimedEvent(info->durability, D_LEVEL_FINEST,
                                  D_THREAD_GROUP_LOCAL_LISTENER,
                                  "No need to request all groups from fellow '%d' again\n",
                                  addr->systemId);
            }
        } else {
            d_printTimedEvent(info->durability, D_LEVEL_FINEST,
                              D_THREAD_GROUP_LOCAL_LISTENER,
                              "No need to request groups from fellow '%d'.\n",
                              addr->systemId);
        }
        d_networkAddressFree(addr);
    }
    return TRUE;
}


static void
doGroupsRequest(
    d_groupLocalListener listener,
    c_iter nameSpaces,
    c_bool iAmAMaster)
{
    d_admin admin;
    struct groupInfo info;

    if(listener){
        admin           = d_listenerGetAdmin(d_listener(listener));

        info.nameSpaces = nameSpaces;
        info.durability = d_adminGetDurability(admin);
        info.iAmAMaster = iAmAMaster;
        info.publisher  = d_adminGetPublisher(admin);
        info.request    = d_groupsRequestNew(admin, NULL, NULL);

        d_adminFellowWalk(admin, requestGroups, &info);

        d_groupsRequestFree(info.request);
    }
    return;
}

/*
struct nsMaster {
    d_nameSpace myNameSpace;
    d_networkAddress masterAddress;
    c_bool conflictsFound;
};

static c_bool
findExistingMaster(
    d_fellow fellow,
    c_voidp userData)
{
    struct nsMaster* nsm;
    d_nameSpace fellowNameSpace;
    d_networkAddress fellowMasterAddress;

    nsm = (struct nsMaster*)userData;
    fellowNameSpace = d_fellowGetNameSpace(fellow, nsm->myNameSpace);

    if(fellowNameSpace){
        fellowMasterAddress = d_nameSpaceGetMaster(fellowNameSpace);


        if(!(d_networkAddressIsUnaddressed(nsm->masterAddress))){
            if( (!d_networkAddressIsUnaddressed(fellowMasterAddress)) &&
                (!d_networkAddressEquals(nsm->masterAddress, fellowMasterAddress)))
            {
                nsm->conflictsFound = TRUE;
            }
            d_networkAddressFree(fellowMasterAddress);
        } else if(!d_networkAddressIsUnaddressed(fellowMasterAddress)){
            nsm->masterAddress = fellowMasterAddress;
        } else {
            //Fellow hasn't determined a master yet
            d_networkAddressFree(fellowMasterAddress);
        }
    } else {
        // This might happen when the fellow has just been added and his
        // namespaces are not complete yet.
        nsm->conflictsFound = TRUE;
    }
    return (!(nsm->conflictsFound));
}

struct nsNewMaster{
    d_nameSpace myNameSpace;
    d_networkAddress bestFellow;
    d_quality fellowQuality;
    d_serviceState bestFellowState;
};

static c_bool
findNewMaster(
    d_fellow fellow,
    c_voidp userData)
{
    struct nsNewMaster* nsm;
    d_nameSpace fellowNameSpace;
    d_networkAddress fellowAddress;
    c_bool isAligner, replace;
    d_serviceState state;
    d_quality quality;

    nsm = (struct nsNewMaster*)userData;
    replace = FALSE;
    fellowNameSpace = d_fellowGetNameSpace(fellow, nsm->myNameSpace);

    if(fellowNameSpace){
        state = d_fellowGetState(fellow);
        isAligner = d_nameSpaceIsAligner(fellowNameSpace);
        quality = d_nameSpaceGetInitialQuality(fellowNameSpace);

        if(isAligner){
            if(!(nsm->bestFellow)){
                replace = TRUE;
            } else if((state > nsm->bestFellowState) &&
                      (state != D_STATE_TERMINATING) &&
                      (state != D_STATE_TERMINATED))
            {
                replace = TRUE;
            } else if(quality.seconds > nsm->fellowQuality.seconds){
                replace = TRUE;
            } else if(quality.seconds == nsm->fellowQuality.seconds){
                if(quality.nanoseconds > nsm->fellowQuality.nanoseconds){
                    replace = TRUE;
                } else if(quality.nanoseconds == nsm->fellowQuality.nanoseconds){
                    fellowAddress = d_fellowGetAddress(fellow);

                    if(d_networkAddressCompare(fellowAddress, nsm->bestFellow) > 0){
                        replace = TRUE;
                    }
                    d_networkAddressFree(fellowAddress);
                }
            }
        }
    }

    if(replace){
        if(nsm->bestFellow){
            d_networkAddressFree(nsm->bestFellow);
        }
        nsm->bestFellow                = d_fellowGetAddress(fellow);
        nsm->fellowQuality.seconds     = quality.seconds;
        nsm->fellowQuality.nanoseconds = quality.nanoseconds;
        nsm->bestFellowState           = state;
    }
    return TRUE;
}


static d_networkAddress
findMaster(
    d_groupLocalListener listener,
    d_nameSpace nameSpace)
{
    d_admin admin;
    d_durability durability;
    d_quality myQuality;
    struct nsMaster nsm;
    struct nsNewMaster nsnm;
    d_networkAddress selectedMaster;

    admin       = d_listenerGetAdmin(d_listener(listener));
    durability  = d_adminGetDurability(admin);

    nsm.myNameSpace    = nameSpace;
    nsm.masterAddress  = d_nameSpaceGetMaster(nameSpace);
    nsm.conflictsFound = FALSE;

    d_adminFellowWalk(admin, findExistingMaster, &nsm);

    if((!d_networkAddressIsUnaddressed(nsm.masterAddress)) && (!nsm.conflictsFound)){
        //Existing master found and found no conflicts
        selectedMaster = nsm.masterAddress;
        d_printTimedEvent(durability, D_LEVEL_INFO,
                        D_THREAD_GROUP_LOCAL_LISTENER,
                        "Found existing master '%d' for nameSpace '%s'.\n",
                        selectedMaster->systemId, d_nameSpaceGetName(nameSpace));

    } else {
        if(nsm.conflictsFound){
            d_printTimedEvent(durability, D_LEVEL_INFO,
                           D_THREAD_GROUP_LOCAL_LISTENER,
                           "Found conflicting masters for nameSpace '%s', " \
                           "determining the master for myself now.\n",
                           d_nameSpaceGetName(nameSpace));
        }
        //Determine master for myself now
        d_networkAddressFree(nsm.masterAddress);
        nsnm.myNameSpace = nameSpace;

        if(d_nameSpaceIsAligner(nameSpace)){
            myQuality                      = d_nameSpaceGetInitialQuality(nameSpace);
            nsnm.bestFellow                = d_adminGetMyAddress(admin);
            nsnm.fellowQuality.seconds     = myQuality.seconds;
            nsnm.fellowQuality.nanoseconds = myQuality.nanoseconds;
            nsnm.bestFellowState           = d_durabilityGetState(durability);
        } else {
            nsnm.bestFellow                = NULL;
            nsnm.fellowQuality.seconds     = 0;
            nsnm.fellowQuality.nanoseconds = 0;
            nsnm.bestFellowState           = D_STATE_INIT;
        }
        d_adminFellowWalk(admin, findNewMaster, &nsnm);

        if(nsnm.bestFellow){
            selectedMaster = nsnm.bestFellow;
            d_printTimedEvent(durability, D_LEVEL_INFO,
                D_THREAD_GROUP_LOCAL_LISTENER,
                "Found new master '%d' for nameSpace '%s'.\n",
                selectedMaster->systemId, d_nameSpaceGetName(nameSpace));
        } else {
            selectedMaster = d_networkAddressUnaddressed();
            d_printTimedEvent(durability, D_LEVEL_INFO,
                    D_THREAD_GROUP_LOCAL_LISTENER,
                    "No master found for nameSpace '%s'.\n",
                    selectedMaster->systemId, d_nameSpaceGetName(nameSpace));
        }
    }
    return selectedMaster;
}
*/
/*****************************************************************************/

struct addressList {
    d_networkAddress address;
    c_ulong count;
    c_voidp next;
};

struct masterInfo {
    d_nameSpace nameSpace;
    d_networkAddress master;
    d_quality masterQuality;
    c_bool conflicts;
    d_serviceState masterState;
    d_durability durability;
    c_bool conflictsAllowed;
    struct addressList* list;
};

static void
addMajorityMaster(
    struct masterInfo* info,
    d_networkAddress address)
{
    struct addressList *list, *prevList;
    c_bool found;

    assert(info);

    found = FALSE;

    if(info->list){
        list = info->list;

        while(list && !found){
            prevList = list;
            if(d_networkAddressEquals(address, list->address)){
                found = TRUE;
                list->count++;
            }
            list = list->next;
        }
        if(!found){
            prevList->next = (struct addressList*)(os_malloc(sizeof(struct addressList)));
            list = prevList->next;
        }
    } else {
        info->list = (struct addressList*)(os_malloc(sizeof(struct addressList)));
        list = info->list;
    }

    if(!found){
        list->address = d_networkAddressNew(address->systemId, address->localId, address->lifecycleId);
        list->count = 1;
        list->next = NULL;
    }
    return;
}

static void
removeMajorityMaster(
    struct masterInfo* info,
    d_networkAddress address)
{
    struct addressList *list, *prev;
    c_bool found;

    assert(info);

    prev = NULL;
    list = info->list;

    while(list && !found){
        if(d_networkAddressEquals(list->address, address)){
            if(prev){
                prev->next = list->next;
            } else {
                info->list = list->next;
            }
            d_networkAddressFree(list->address);
            os_free(list);
            found = TRUE;
        }
        prev = list;
        list = list->next;
    }
    return;
}

static d_networkAddress
getMajorityMaster(
    struct masterInfo* info)
{
    d_networkAddress master;
    c_ulong count;
    c_bool replace;
    struct addressList* list;

    assert(info);

    if(info->list){
        master = d_networkAddressNew(info->list->address->systemId,
                info->list->address->localId, info->list->address->lifecycleId);
        count = info->list->count;

        list = (struct addressList*)(info->list->next);

        while(list){
            replace = FALSE;

            if(list->count > count){
                replace = TRUE;
            } else if (list->count == count){
                if(d_networkAddressCompare(list->address, master) > 0){
                    replace = TRUE;
                }
            }

            if(replace){
                d_networkAddressFree(master);
                master = d_networkAddressNew(list->address->systemId,
                        list->address->localId, list->address->lifecycleId);
                count = list->count;
            }
            list = (struct addressList*)list->next;
        }
    } else {
        master = d_networkAddressUnaddressed();
    }
    return master;
}

static void
freeMajorityMasters(
    struct masterInfo* info)
{
    struct addressList *list, *prevList;

    assert(info);

    list = info->list;

    while(list){
        d_networkAddressFree(list->address);
        prevList = list;
        list = list->next;
        os_free(prevList);
    }
    return;
}

static c_bool
determineExistingMaster(
    d_fellow fellow,
    c_voidp userData)
{
    struct masterInfo* m;
    d_nameSpace fellowNameSpace;
    d_networkAddress fellowMaster, fellowAddress;
    c_bool result;

    m = (struct masterInfo*)userData;
    fellowNameSpace = d_fellowGetNameSpace(fellow, m->nameSpace);

    /*Check if fellow has compatible nameSpace. If not go to the next one */
    if(fellowNameSpace){
        fellowMaster = d_nameSpaceGetMaster(fellowNameSpace);

        /* If I haven't found a potential master so far, check whether the
         * fellow determined a (potential) master already. If so, conform
         * to the selected master.
         */
        if(d_networkAddressIsUnaddressed(m->master)){
            if(!d_networkAddressIsUnaddressed(fellowMaster)){
                d_networkAddressFree(m->master);
                m->master = d_networkAddressNew(
                        fellowMaster->systemId,
                        fellowMaster->localId,
                        fellowMaster->lifecycleId);
                addMajorityMaster(m, fellowMaster);
            }
        } else if(!d_networkAddressIsUnaddressed(fellowMaster)){
            /* If the fellow has determined a (potential) master
             * that doesn't match my current one, there's a conflict and
             * I need to drop out of this function.
             */
            if(!d_networkAddressEquals(m->master, fellowMaster)){
                m->conflicts = TRUE;

                fellowAddress = d_fellowGetAddress(fellow);
                d_printTimedEvent(m->durability, D_LEVEL_INFO,
                    D_THREAD_GROUP_LOCAL_LISTENER,
                    "Fellow '%d' reports master '%d' for nameSpace '%s'. " \
                    "while I found master '%d'.\n",
                    fellowAddress->systemId,
                    fellowMaster->systemId,
                    d_nameSpaceGetName(m->nameSpace),
                    m->master->systemId);
                d_networkAddressFree(fellowAddress);
            }
            addMajorityMaster(m, fellowMaster);
        }
        d_networkAddressFree(fellowMaster);
    }
    if(m->conflictsAllowed){
        result = TRUE;
    } else {
        result = !(m->conflicts);
    }

    return result;
}

static c_bool
determineNewMaster(
    d_fellow fellow,
    c_voidp userData)
{
    struct masterInfo* m;
    d_nameSpace fellowNameSpace;
    d_networkAddress fellowAddress;
    c_bool isAligner, replace;
    d_quality quality;
    d_serviceState fellowState;

    m = (struct masterInfo*)userData;
    replace = FALSE;
    fellowNameSpace = d_fellowGetNameSpace(fellow, m->nameSpace);

    if(fellowNameSpace){
        isAligner = d_nameSpaceIsAligner(fellowNameSpace);
        quality = d_nameSpaceGetInitialQuality(fellowNameSpace);
        fellowState = d_fellowGetState(fellow);

        if(isAligner){
            if(d_networkAddressIsUnaddressed(m->master)){
                replace = TRUE;
            } else if((m->masterState <= D_STATE_DISCOVER_PERSISTENT_SOURCE) && (fellowState > D_STATE_DISCOVER_PERSISTENT_SOURCE)) {
                replace = TRUE;
            } else if((m->masterState > D_STATE_DISCOVER_PERSISTENT_SOURCE) && (fellowState <= D_STATE_DISCOVER_PERSISTENT_SOURCE)) {
                replace = FALSE;
            } else if(quality.seconds > m->masterQuality.seconds){
                replace = TRUE;
            } else if(quality.seconds == m->masterQuality.seconds){
                if(quality.nanoseconds > m->masterQuality.nanoseconds){
                    replace = TRUE;
                } else if(quality.nanoseconds == m->masterQuality.nanoseconds){
                    fellowAddress = d_fellowGetAddress(fellow);

                    if(d_networkAddressCompare(fellowAddress, m->master) > 0){
                        replace = TRUE;
                    }
                    d_networkAddressFree(fellowAddress);
                }
            }
        }
    }

    if(replace){
        if(m->master){
            d_networkAddressFree(m->master);
        }
        m->master = d_fellowGetAddress(fellow);
        m->masterQuality.seconds = quality.seconds;
        m->masterQuality.nanoseconds = quality.nanoseconds;
        m->masterState = fellowState;
    }
    return TRUE;
}

/*
 * PRECONDTION: listener->masterLock is locked
 *
 * @return Returns TRUE if I have become a master for one or more nameSpaces.
 */
static c_bool
d_groupLocalListenerDetermineMasters(
    d_groupLocalListener listener,
    c_iter nameSpaces)
{
    d_admin admin;
    d_durability durability;
    d_configuration configuration;
    c_long length, i;
    d_nameSpace nameSpace;
    d_subscriber subscriber;
    d_nameSpacesRequestListener nsrListener;
    d_networkAddress unaddressed, myAddress, master, lastMaster;
    struct masterInfo mastership;
    os_time sleepTime, endTime;
    c_bool nsComplete, proceed, conflicts, firstTime, cont;
    d_quality myQuality;
    c_bool iAmAMaster;
    d_serviceState myState, fellowState;
    d_fellow fellow;
    c_ulong tries, maxTries;

    admin = d_listenerGetAdmin(d_listener(listener));
    durability = d_adminGetDurability(admin);
    configuration = d_durabilityGetConfiguration(durability);
    length = c_iterLength(nameSpaces);
    subscriber = d_adminGetSubscriber(admin);
    nsrListener = d_subscriberGetNameSpacesRequestListener(subscriber);
    unaddressed = d_networkAddressUnaddressed();
    myAddress = d_adminGetMyAddress(admin);
    firstTime = TRUE;
    iAmAMaster = FALSE;
    maxTries = 4;
    tries = 0;

    /*100ms*/
    sleepTime.tv_sec = 0;
    sleepTime.tv_nsec = 100000000;
    mastership.durability = durability;

    do {
        conflicts = FALSE;
        tries++;

        /*Check if namespaces still have the same master*/
        for(i=0; i<length; i++){
            nameSpace = d_nameSpace(c_iterObject(nameSpaces, i));

            /* Remember the last master I selected. If the 'existing'
             * one I find now is different, I need to do this whole loop again.
             */
            if(firstTime){
                lastMaster = d_networkAddressUnaddressed();
            } else {
                lastMaster = d_nameSpaceGetMaster(nameSpace);
            }
            /*
             * Always use un-addressed as my master when looking for an
             * existing master.
             */
            d_nameSpaceSetMaster(nameSpace, unaddressed);

            mastership.nameSpace = nameSpace;
            mastership.master = d_nameSpaceGetMaster(nameSpace);
            mastership.conflicts = FALSE;
            mastership.list = NULL;

            if(tries >= maxTries){
                mastership.conflictsAllowed = TRUE;
            } else {
                mastership.conflictsAllowed = FALSE;
            }

            /*Walk over all fellows that are approved*/
            d_adminFellowWalk(admin, determineExistingMaster, &mastership);

            if((!mastership.conflicts) && (!d_networkAddressIsUnaddressed(mastership.master))){
                /*Check whether the found fellow is still alive and kicking.*/

                /*Some node(s) could already have chosen me as master */
                if(!d_networkAddressEquals(mastership.master, myAddress)){
                    fellow = d_adminGetFellow(admin, mastership.master);

                    /*Fellow may be gone already or is currently terminating*/
                    if(!fellow){
                        d_networkAddressFree(mastership.master);
                        mastership.master = d_networkAddressUnaddressed();
                    } else {
                        fellowState = d_fellowGetState(fellow);

                        if((fellowState == D_STATE_TERMINATING) || (fellowState == D_STATE_TERMINATED)){
                            d_networkAddressFree(mastership.master);
                            mastership.master = d_networkAddressUnaddressed();
                        } else if(!d_networkAddressEquals(lastMaster, mastership.master)){
                            /* Check whether I determined an master already
                             * in the previous loop.
                             */
                            if(!d_networkAddressIsUnaddressed(lastMaster)) {
                                /* Existing master is not the same as the one I found before
                                 * so make sure I'll check once more.
                                 */
                                conflicts = TRUE;
                                d_printTimedEvent(durability, D_LEVEL_INFO,
                                    D_THREAD_GROUP_LOCAL_LISTENER,
                                    "The existing master '%d' I found now for nameSpace '%s'. " \
                                    "doesn't match the one '%d' I found before. " \
                                    "Waiting for confirmation...",
                                    mastership.master->systemId,
                                    d_nameSpaceGetName(nameSpace),
                                    lastMaster->systemId);
                            }
                        }
                        d_fellowFree(fellow);
                    }
                }
            } else if(mastership.conflictsAllowed){
                cont = TRUE;
                mastership.conflicts = FALSE;

                while(cont){
                    if(mastership.master){
                        d_networkAddressFree(mastership.master);
                    }
                    mastership.master = getMajorityMaster(&mastership);

                    if(!d_networkAddressIsUnaddressed(mastership.master)){
                        fellow = d_adminGetFellow(admin, mastership.master);

                        /*Fellow may be gone already or is currently terminating*/
                        if(!fellow){
                            removeMajorityMaster(&mastership, mastership.master);
                        } else {
                            fellowState = d_fellowGetState(fellow);

                            if((fellowState == D_STATE_TERMINATING) || (fellowState == D_STATE_TERMINATED)){
                                removeMajorityMaster(&mastership, mastership.master);
                            } else {
                                d_printTimedEvent(durability, D_LEVEL_INFO,
                                    D_THREAD_GROUP_LOCAL_LISTENER,
                                    "Found majority master '%d' for nameSpace '%s'.\n",
                                    mastership.master->systemId,
                                    d_nameSpaceGetName(nameSpace));
                                cont = FALSE;
                            }
                            d_fellowFree(fellow);
                        }
                    } else {
                        d_printTimedEvent(durability, D_LEVEL_INFO,
                            D_THREAD_GROUP_LOCAL_LISTENER,
                            "Tried to find majority master for nameSpace '%s', but found none.\n",
                            d_nameSpaceGetName(nameSpace));
                        cont = FALSE;
                    }
                }
            }
            freeMajorityMasters(&mastership);

            if((mastership.conflicts) || d_networkAddressIsUnaddressed(mastership.master)){
                if(mastership.conflicts){
                    d_printTimedEvent(durability, D_LEVEL_INFO,
                        D_THREAD_GROUP_LOCAL_LISTENER,
                        "Found conflicting masters for nameSpace '%s'. " \
                        "Determining new master now...\n",
                        d_nameSpaceGetName(nameSpace));
                    conflicts = TRUE;
                    mastership.conflicts = FALSE;
                } else {
                    d_printTimedEvent(durability, D_LEVEL_INFO,
                        D_THREAD_GROUP_LOCAL_LISTENER,
                        "Found no existing master for nameSpace '%s'. " \
                        "Determining new master now...\n",
                        d_nameSpaceGetName(nameSpace));
                }
                d_networkAddressFree(mastership.master);

                if(d_nameSpaceIsAligner(nameSpace)){
                    myQuality = d_nameSpaceGetInitialQuality(nameSpace);
                    mastership.master = d_adminGetMyAddress(admin);
                    mastership.masterQuality.seconds = myQuality.seconds;
                    mastership.masterQuality.nanoseconds = myQuality.nanoseconds;
                    mastership.masterState = d_durabilityGetState(durability);
                } else {
                    mastership.master = d_networkAddressUnaddressed();
                    mastership.masterQuality.seconds = 0;
                    mastership.masterQuality.nanoseconds = 0;
                    mastership.masterState = D_STATE_INIT;
                }
                /*7. Walk over all fellows that are approved again.*/
                d_adminFellowWalk(admin, determineNewMaster, &mastership);

                if(d_networkAddressIsUnaddressed(mastership.master)){
                    d_printTimedEvent(durability, D_LEVEL_INFO,
                       D_THREAD_GROUP_LOCAL_LISTENER,
                       "There's no new master available for nameSpace '%s'. " \
                       "Awaiting availability of a new master...\n",
                       d_nameSpaceGetName(nameSpace));
                } else if(d_networkAddressEquals(mastership.master, myAddress)){
                    d_printTimedEvent(durability, D_LEVEL_INFO,
                       D_THREAD_GROUP_LOCAL_LISTENER,
                       "I want to be the new master for nameSpace '%s'. " \
                       "Waiting for confirmation...\n",
                       d_nameSpaceGetName(nameSpace));
                } else {
                    d_printTimedEvent(durability, D_LEVEL_INFO,
                        D_THREAD_GROUP_LOCAL_LISTENER,
                        "I want fellow '%d' to be the new master for nameSpace '%s'. " \
                        "Waiting for confirmation...\n",
                        mastership.master->systemId,
                        d_nameSpaceGetName(nameSpace));
                }
            } else {
                d_printTimedEvent(durability, D_LEVEL_INFO,
                    D_THREAD_GROUP_LOCAL_LISTENER,
                    "Found existing master '%d' for nameSpace '%s'. " \
                    "Waiting for confirmation...\n",
                    mastership.master->systemId,
                    d_nameSpaceGetName(nameSpace));
            }
            d_nameSpaceSetMaster(nameSpace, mastership.master);
            d_networkAddressFree(mastership.master);
            d_networkAddressFree(lastMaster);
        }
        d_nameSpacesRequestListenerReportNameSpaces(nsrListener);

        /* Do the same thing again if there were conflicts.
         * Since this step must be taken at least once (even if there are
         * no conflicts), 'firstTime' is checked also.
         */
        if(conflicts || firstTime){
            if(firstTime){
                firstTime = FALSE;
                /*To make sure the do-while is done at least once more: */
                conflicts = TRUE;
            }
            /*Wait twice the heartbeat period*/
            endTime = os_timeGet();
            endTime = os_timeAdd(endTime, configuration->heartbeatUpdateInterval);
            endTime = os_timeAdd(endTime, configuration->heartbeatUpdateInterval);

            d_printTimedEvent(durability, D_LEVEL_INFO,
                       D_THREAD_GROUP_LOCAL_LISTENER,
                       "Waiting twice the heartbeat period: 2*%f seconds.\n",
                       os_timeToReal(configuration->heartbeatUpdateInterval));

            nsComplete = TRUE;
            proceed = TRUE;

            while(d_durabilityMustTerminate(durability) == FALSE && proceed ){
                os_nanoSleep(sleepTime);

                if(os_timeCompare(os_timeGet(), endTime) > 0){
                    proceed = FALSE;
                    d_adminFellowWalk(admin, checkNameSpaces, &nsComplete);
                    proceed = !nsComplete;
                }
            }
        } else if(d_durabilityMustTerminate(durability) == FALSE){
            assert(conflicts == FALSE);
            /*No more conflicts; all masters have been confirmed*/
            for(i=0; i<length; i++){
                nameSpace = d_nameSpace(c_iterObject(nameSpaces, i));
                master = d_nameSpaceGetMaster(nameSpace);

                if(d_networkAddressEquals(master, myAddress)){
                    d_printTimedEvent(durability, D_LEVEL_INFO,
                       D_THREAD_GROUP_LOCAL_LISTENER,
                       "Confirming master: I am the master for nameSpace '%s'.\n",
                       d_nameSpaceGetName(nameSpace));
                    iAmAMaster = TRUE;
                } else {
                    d_printTimedEvent(durability, D_LEVEL_INFO,
                           D_THREAD_GROUP_LOCAL_LISTENER,
                           "Confirming master: Fellow '%d' is the master for nameSpace '%s'.\n",
                           master->systemId,
                           d_nameSpaceGetName(nameSpace));
                }
                d_networkAddressFree(master);
            }
        }
    } while ((conflicts == TRUE) && (d_durabilityMustTerminate(durability) == FALSE));

    os_mutexUnlock(&listener->masterLock);

    d_durabilityUpdateStatistics(durability, d_statisticsUpdateConfiguration, admin);
    myState = d_durabilityGetState(durability);

    if(myState != D_STATE_COMPLETE){
        doGroupsRequest(listener, nameSpaces, iAmAMaster);
    }
    d_networkAddressFree(unaddressed);
    d_networkAddressFree(myAddress);

    return iAmAMaster;
}

/*****************************************************************************/

static void
initPersistentData(
    d_groupLocalListener listener)
{
    d_admin admin;
    d_subscriber subscriber;
    d_store store;
    d_durability durability;
    d_group group;
    d_configuration configuration;
    u_participant participant;
    d_storeResult result;
    d_groupList list, next;
    c_long i, length;
    d_nameSpace nameSpace;
    d_durabilityKind dkind;
    c_bool attached;
    v_group vgroup;

    admin         = d_listenerGetAdmin(d_listener(listener));
    durability    = d_adminGetDurability(admin);
    subscriber    = d_adminGetSubscriber(admin);
    store         = d_subscriberGetPersistentStore(subscriber);
    configuration = d_durabilityGetConfiguration(durability);
    length        = c_iterLength(configuration->nameSpaces);
    participant   = u_participant(d_durabilityGetService(durability));
    result        = d_storeGroupsRead(store, &list);

    if(result == D_STORE_RESULT_OK){
        for(i=0; (i<length) && (d_durabilityMustTerminate(durability) == FALSE); i++) {
            nameSpace = d_nameSpace(c_iterObject(configuration->nameSpaces, i));

            if(d_nameSpaceMasterIsMe(nameSpace, admin)){
                dkind = d_nameSpaceGetDurabilityKind(nameSpace);

                if((dkind == D_DURABILITY_PERSISTENT) || (dkind == D_DURABILITY_ALL)){
                    next = list;

                    d_durabilitySetState(durability, D_STATE_INJECT_PERSISTENT);

                    while(next) {
                        if(d_durabilityMustTerminate(durability) == FALSE){
                            if(d_nameSpaceIsIn(nameSpace, next->partition, next->topic) == TRUE) {
                                result = d_storeGroupInject(store, next->partition, next->topic, participant, &group);

                                if(result == D_STORE_RESULT_OK) {
                                    d_printTimedEvent(durability, D_LEVEL_FINE,
                                        D_THREAD_GROUP_LOCAL_LISTENER,
                                        "Group %s.%s locally created\n",
                                        next->partition, next->topic);


                                    d_printTimedEvent(durability, D_LEVEL_FINE,
                                        D_THREAD_GROUP_LOCAL_LISTENER,
                                        "Data from group %s.%s must now be injected\n",
                                        next->partition, next->topic);

                                    vgroup = d_groupGetKernelGroup(group);
                                    attached = d_durabilityWaitForAttachToGroup(durability, vgroup);
                                    c_free(vgroup);

                                    result = d_storeMessagesInject(store, group);

                                    if(result == D_STORE_RESULT_OK) {
                                        d_printTimedEvent(durability, D_LEVEL_FINE,
                                            D_THREAD_GROUP_LOCAL_LISTENER,
                                            "All data for group %s.%s has been injected from local store.\n",
                                            next->partition, next->topic);
                                    } else {
                                        d_printTimedEvent(durability, D_LEVEL_SEVERE,
                                            D_THREAD_GROUP_LOCAL_LISTENER,
                                            "All data for group %s.%s could not be injected.\n",
                                            next->partition, next->topic);
                                    }
                                    if(!attached){
                                        d_groupSetPrivate(group, TRUE);
                                    }
                                    d_groupSetComplete(group);
                                    d_adminAddLocalGroup(admin, group);
                                    d_sampleChainListenerReportGroup(listener->sampleChainListener, group);
                                } else {
                                    d_printTimedEvent(durability, D_LEVEL_SEVERE,
                                        D_THREAD_GROUP_LOCAL_LISTENER,
                                        "Group %s.%s could NOT be created locally (%d)\n",
                                        next->partition, next->topic, result);
                                }
                            } else {
                                d_printTimedEvent(durability, D_LEVEL_FINE,
                                            D_THREAD_GROUP_LOCAL_LISTENER,
                                            "Group %s.%s not in nameSpace.\n",
                                            next->partition, next->topic);
                            }
                        }
                        next = d_groupList(next->next);
                    }
                }
            } else {
                result = d_storeBackup(store, nameSpace);

                if(result != D_STORE_RESULT_OK) {
                    d_printTimedEvent(durability, D_LEVEL_SEVERE,
                        D_THREAD_GROUP_LOCAL_LISTENER,
                        "Namespace could NOT be backupped in local persistent store (%d)\n",
                        result);
                }
            }
        }
    } else {
        d_printTimedEvent(durability, D_LEVEL_SEVERE,
                            D_THREAD_GROUP_LOCAL_LISTENER,
                            "Could not read groups from persistent store. Persistent data not injected.\n");
    }
}

/*
 * PRECONDTION: listener->masterLock is locked
 */
static void
initMasters(
    d_groupLocalListener listener)
{
    d_admin admin;
    d_durability durability;
    d_configuration configuration;
    c_bool fellowGroupsKnown, terminate;
    os_time sleepTime;

    admin = d_listenerGetAdmin(d_listener(listener));
    durability = d_adminGetDurability(admin);
    configuration = d_durabilityGetConfiguration(durability);

    d_groupLocalListenerDetermineMasters(listener, configuration->nameSpaces);

    fellowGroupsKnown = FALSE;
    sleepTime.tv_sec  = 0;
    sleepTime.tv_nsec = 100000000; /* 100 ms */
    terminate = d_durabilityMustTerminate(durability);

    /*now wait for completion of groups*/
    while((fellowGroupsKnown == FALSE) && (!terminate) && (d_adminGetFellowCount(admin) > 0)){
        d_adminFellowWalk(admin, checkFellowGroupsKnown, &fellowGroupsKnown);
        os_nanoSleep(sleepTime);
        terminate = d_durabilityMustTerminate(durability);
    }
    if(!terminate){
        d_printTimedEvent(durability, D_LEVEL_FINE, D_THREAD_MAIN, "Fellow groups complete.\n");
    }
}

struct masterHelper {
    d_groupLocalListener listener;
    c_iter nameSpaces;
};

static c_bool
determineNewMastersAction(
    d_action action,
    c_bool terminate)
{
    struct masterHelper* helper;
    d_groupLocalListener listener;
    c_bool tryChains;

    helper = (struct masterHelper*)(d_actionGetArgs(action));

    if(terminate == FALSE){
        listener = helper->listener;

        if(d_objectIsValid(d_object(listener), D_LISTENER)){
            os_mutexLock(&listener->masterLock);

            if(c_iterLength(helper->nameSpaces) > 0){
                tryChains = d_groupLocalListenerDetermineMasters(listener, helper->nameSpaces);
            }

            os_mutexUnlock(&listener->masterLock);

            if(tryChains){
                d_sampleChainListenerTryFulfillChains(listener->sampleChainListener, NULL);
            }
        }
    }
    c_iterFree(helper->nameSpaces);
    os_free(helper);

    return FALSE;
}

static c_bool
notifyFellowRemoved(
    c_ulong event,
    d_fellow fellow,
    d_group group,
    c_voidp userData)
{
    d_groupLocalListener listener;
    d_admin admin;
    d_durability durability;
    d_configuration configuration;
    c_long length, i;
    d_nameSpace nameSpace;
    d_action masterAction;
    os_time sleepTime;
    d_networkAddress masterAddress, fellowAddress;
    c_iter nameSpaces;
    struct masterHelper *helper;

    if(event == D_FELLOW_REMOVED){
        listener      = d_groupLocalListener(userData);
        admin         = d_listenerGetAdmin(d_listener(listener));
        durability    = d_adminGetDurability(admin);
        configuration = d_durabilityGetConfiguration(durability);
        length        = c_iterLength(configuration->nameSpaces);
        fellowAddress = d_fellowGetAddress(fellow);
        nameSpaces    = c_iterNew(NULL);

        d_printTimedEvent(durability, D_LEVEL_INFO,
                    D_THREAD_GROUP_LOCAL_LISTENER,
                    "Fellow '%d' removed, checking whether new master must be determined.\n",
                    fellowAddress->systemId);

        for(i=0; (i<length) && (d_durabilityMustTerminate(durability) == FALSE); i++) {
            nameSpace = d_nameSpace(c_iterObject(configuration->nameSpaces, i));
            masterAddress = d_nameSpaceGetMaster(nameSpace);

            if((d_networkAddressEquals(masterAddress, fellowAddress)) ||
               (d_networkAddressIsUnaddressed(masterAddress)))
            {
                d_printTimedEvent(durability, D_LEVEL_INFO,
                                D_THREAD_GROUP_LOCAL_LISTENER,
                                "Need to find a new master for nameSpace '%s'.\n",
                                d_nameSpaceGetName(nameSpace));

                nameSpaces = c_iterInsert(nameSpaces, nameSpace);
            } else {
                d_printTimedEvent(durability, D_LEVEL_INFO,
                                D_THREAD_GROUP_LOCAL_LISTENER,
                                "Master '%d' still available for nameSpace '%s'.\n",
                                masterAddress->systemId, d_nameSpaceGetName(nameSpace));
            }
            d_networkAddressFree(masterAddress);
        }
        length = c_iterLength(nameSpaces);

        if(length > 0){
            helper = (struct masterHelper*)(os_malloc(sizeof(struct masterHelper)));
            helper->listener = listener;
            helper->nameSpaces = nameSpaces;
            sleepTime.tv_sec = 0;
            sleepTime.tv_nsec = 100000000;
            masterAction = d_actionNew(os_timeGet(), sleepTime, determineNewMastersAction, helper);
            d_actionQueueAdd(listener->masterMonitor, masterAction);
        } else {
            c_iterFree(nameSpaces);
        }
        d_networkAddressFree(fellowAddress);
    }
    return TRUE;

}
/*************** END NEW IMPL *************************/

void
d_groupLocalListenerInit(
    d_groupLocalListener listener,
    d_subscriber subscriber)
{
    os_time sleepTime;
    os_mutexAttr attr;
    os_threadAttr ta;

    d_listenerInit(d_listener(listener), subscriber, NULL, d_groupLocalListenerDeinit);
    assert(d_objectIsValid(d_object(listener), D_LISTENER) == TRUE);

    sleepTime.tv_sec                     = 0;
    sleepTime.tv_nsec                    = 100000000;
    listener->lastSequenceNumber         = D_FLOOR_SEQUENCE_NUMBER;
    listener->initialGroupsAdministrated = FALSE;

    os_threadAttrInit(&ta);
    listener->actionQueue                = d_actionQueueNew(
                                                "groupLocalListenerActionQueue",
                                                sleepTime, ta);

    listener->masterMonitor              = d_actionQueueNew(
                                                "masterMonitor",
                                                 sleepTime, ta);
    os_mutexAttrInit(&attr);
    attr.scopeAttr = OS_SCOPE_PRIVATE;
    os_mutexInit(&(listener->masterLock), &attr);

    listener->fellowListener = d_eventListenerNew(
                                        D_FELLOW_REMOVED,
                                        notifyFellowRemoved,
                                        listener);
}

static c_bool
d_groupLocalAction(
    d_action action,
    c_bool terminate)
{
    d_listener listener;
    d_durability durability;
    d_admin admin;
    u_entity service;

    listener = d_listener(d_actionGetArgs(action));

    if(d_objectIsValid(d_object(listener), D_LISTENER)){
        if(terminate == FALSE){
            admin = d_listenerGetAdmin(listener);
            durability = d_adminGetDurability(admin);
            service = u_entity(d_durabilityGetService(durability));

            u_entityAction(service, d_groupLocalListenerHandleNewGroupsLocal, listener);
        }
    }
    return FALSE;
}


struct deleteHistoricalDataHelper {
    c_time deleteTime;
    c_char* partExpr;
    c_char* topicExpr;
    d_listener listener;
};

static c_bool
d_groupDeleteHistoricalDataAction(
    d_action action,
    c_bool terminate)
{
    d_durability durability;
    d_admin admin;
    d_publisher publisher;
    d_networkAddress unaddressed;
    d_deleteData delData;
    struct deleteHistoricalDataHelper* dhd;

    dhd = (struct deleteHistoricalDataHelper*)(d_actionGetArgs(action));

    if(d_objectIsValid(d_object(dhd->listener), D_LISTENER)){
        if(terminate == FALSE){
            admin = d_listenerGetAdmin(dhd->listener);
            durability = d_adminGetDurability(admin);
            publisher = d_adminGetPublisher(admin);

            d_printTimedEvent(durability, D_LEVEL_FINER,
                D_THREAD_GROUP_LOCAL_LISTENER,
                "Notified fellows of delete_historical_data action.\n");

            unaddressed = d_networkAddressUnaddressed();
            delData = d_deleteDataNew(admin, dhd->deleteTime, dhd->partExpr, dhd->topicExpr);
            d_publisherDeleteDataWrite(publisher, delData, unaddressed);
            d_networkAddressFree(unaddressed);
            d_deleteDataFree(delData);
        }
    }
    os_free(dhd->partExpr);
    os_free(dhd->topicExpr);
    os_free(dhd);

    return FALSE;
}

struct readerRequestHelper {
    d_readerRequest request;
    d_admin admin;
    d_groupLocalListener listener;
};

static c_bool
d_groupLocalReaderRequestAction(
    d_action action,
    c_bool terminate)
{
    c_bool callAgain, fulfilled;
    d_table groups;
    d_group group, localGroup;
    c_char *partition, *topic;
    d_durabilityKind kind;
    d_durability durability;
    v_handle handle;
    struct readerRequestHelper* helper;

    helper = (struct readerRequestHelper*)d_actionGetArgs(action);

    if(!terminate){
        callAgain  = FALSE;
        groups     = d_readerRequestGetGroups(helper->request);
        durability = d_adminGetDurability(helper->admin);
        handle     = d_readerRequestGetHandle(helper->request);

        group = d_tableTake(groups);

        while(group && !callAgain){
            partition  = d_groupGetPartition(group);
            topic      = d_groupGetTopic(group);
            kind       = d_groupGetKind(group);
            localGroup = d_adminGetLocalGroup(helper->admin, partition, topic, kind);

            if(!localGroup){
                printf("CallAgain\n");
                callAgain = TRUE;
            } else {
                d_printTimedEvent(durability, D_LEVEL_FINER,
                    D_THREAD_GROUP_LOCAL_LISTENER,
                    "Handling alignment of group %s.%s as part of "\
                    "historicalDataRequest from reader [%d, %d]\n",
                    partition, topic, handle.index, handle.serial);
                d_groupLocalListenerHandleAlignment(helper->listener,
                        localGroup, helper->request);
            }
            os_free(partition);
            os_free(topic);

            d_groupFree(group);

            if(!callAgain){
                group = d_tableTake(groups);
            } else {
                group = NULL;
            }
        }
        d_tableFree(groups);

        if(!callAgain){
            d_printTimedEvent(durability, D_LEVEL_FINER,
                                D_THREAD_GROUP_LOCAL_LISTENER,
                                "Alignment for historicalDataRequest from "\
                                "reader [%d, %d] in progress\n",
                                handle.index, handle.serial);
            fulfilled = d_adminCheckReaderRequestFulfilled(
                                helper->admin, helper->request);

            if(fulfilled){
                d_printTimedEvent(durability, D_LEVEL_FINER,
                        D_THREAD_GROUP_LOCAL_LISTENER,
                        "historicalDataRequest from reader [%d, %d] fulfilled.\n",
                         handle.index, handle.serial);
            }
        }
    } else {
        callAgain = FALSE;
    }


    if(!callAgain){
        d_readerRequestFree(helper->request);
        os_free(helper);
    }

    return callAgain;
}

c_ulong
d_groupLocalListenerAction(
    u_dispatcher o,
    u_waitsetEvent event,
    c_voidp userData)
{
    d_listener listener;
    d_admin admin;
    d_durability durability;
    d_actionQueue queue;
    d_action action;
    u_waitsetHistoryDeleteEvent hde;
    u_waitsetHistoryRequestEvent hre;
    os_time sleepTime;
    d_readerRequest readerRequest;
    struct deleteHistoricalDataHelper* data;
    struct readerRequestHelper* requestHelper;

    if (o && userData) {
        listener   = d_listener(userData);
        assert(d_listenerIsValid(d_listener(listener), D_GROUP_LOCAL_LISTENER));
        admin      = d_listenerGetAdmin(listener);
        durability = d_adminGetDurability(admin);
        queue      = d_groupLocalListener(listener)->actionQueue;

        if((event->events & V_EVENT_NEW_GROUP) == V_EVENT_NEW_GROUP){
            sleepTime.tv_sec = 1;
            sleepTime.tv_nsec = 0;
            action = d_actionNew(os_timeGet(), sleepTime, d_groupLocalAction, listener);
            d_actionQueueAdd(queue, action);
        }
        if((event->events & V_EVENT_HISTORY_REQUEST) == V_EVENT_HISTORY_REQUEST){
            hre = u_waitsetHistoryRequestEvent(event);

            d_printTimedEvent(durability, D_LEVEL_FINER,
                    D_THREAD_GROUP_LOCAL_LISTENER,
                    "Received historicalDataRequest from reader [%d, %d]\n",
                    hre->source.index, hre->source.serial);
            readerRequest = d_readerRequestNew(admin,
                            hre->source, hre->filter, hre->filterParams,
                            hre->filterParamsCount, hre->resourceLimits,
                            hre->minSourceTimestamp, hre->maxSourceTimestamp);
            d_adminAddReaderRequest(admin, readerRequest);

            sleepTime.tv_sec  = 0;
            sleepTime.tv_nsec = 500 * 1000 * 1000; /* 500ms*/
            requestHelper = (struct readerRequestHelper*)
                                os_malloc(sizeof(struct readerRequestHelper));
            requestHelper->admin = admin;
            requestHelper->listener = d_groupLocalListener(listener);
            requestHelper->request = readerRequest;

            action = d_actionNew(os_timeGet(),
                    sleepTime, d_groupLocalReaderRequestAction, requestHelper);
            d_actionQueueAdd(queue, action);
        }

        if((event->events & V_EVENT_HISTORY_DELETE) == V_EVENT_HISTORY_DELETE){
            d_printTimedEvent(durability, D_LEVEL_FINER,
                D_THREAD_GROUP_LOCAL_LISTENER,
                "Received local deleteHistoricalData event. Notifying fellows...\n");

            hde   = u_waitsetHistoryDeleteEvent(event);
            data  = (struct deleteHistoricalDataHelper*)(os_malloc(
                                    sizeof(struct deleteHistoricalDataHelper)));

            data->deleteTime.seconds     = hde->deleteTime.seconds;
            data->deleteTime.nanoseconds = hde->deleteTime.nanoseconds;
            data->partExpr               = os_strdup(hde->partitionExpr);
            data->topicExpr              = os_strdup(hde->topicExpr);
            data->listener               = listener;

            sleepTime.tv_sec             = 1;
            sleepTime.tv_nsec            = 0;

            action = d_actionNew(os_timeGet(), sleepTime, d_groupDeleteHistoricalDataAction, data);
            d_actionQueueAdd(queue, action);
        }
    }
    return event->events;

}

c_bool
d_groupLocalListenerStart(
    d_groupLocalListener listener)
{
    c_bool result;
    u_dispatcher dispatcher;
    u_result ur;
    c_ulong mask;
    d_durability durability;

    c_bool wsResult;
    d_waitset waitset;
    d_admin admin;
    d_subscriber subscriber;
    d_waitsetAction action;
    d_store store;
    d_configuration config;
    os_threadAttr attr;

    result = FALSE;

    assert(d_listenerIsValid(d_listener(listener), D_GROUP_LOCAL_LISTENER));

    if(listener){
        d_listenerLock(d_listener(listener));
        durability  = d_adminGetDurability(d_listenerGetAdmin(d_listener(listener)));
        config      = d_durabilityGetConfiguration(durability);
        dispatcher  = u_dispatcher( d_durabilityGetService(durability));
        action      = d_groupLocalListenerAction;

        if(d_listener(listener)->attached == FALSE){
            ur = u_dispatcherGetEventMask(dispatcher, &mask);

            if(ur == U_RESULT_OK){
                ur = u_dispatcherSetEventMask(dispatcher,
                        mask | V_EVENT_NEW_GROUP | V_EVENT_HISTORY_DELETE | V_EVENT_HISTORY_REQUEST);

                if(ur == U_RESULT_OK){
                    admin      = d_listenerGetAdmin(d_listener(listener));
                    subscriber = d_adminGetSubscriber(admin);
                    store      = d_subscriberGetPersistentStore(subscriber);
                    waitset    = d_subscriberGetWaitset(subscriber);

                    os_threadAttrInit(&attr);
                    listener->waitsetData = d_waitsetEntityNew(
                                    "groupLocalListener",
                                    dispatcher, action,
                                    V_EVENT_NEW_GROUP | V_EVENT_HISTORY_DELETE | V_EVENT_HISTORY_REQUEST,
                                    attr, listener);
                    wsResult = d_waitsetAttach(waitset, listener->waitsetData);

                    if(wsResult == TRUE) {
                        ur = U_RESULT_OK;
                    } else {
                        ur = U_RESULT_ILL_PARAM;
                    }

                    if(listener->initialGroupsAdministrated == FALSE){
                        d_durabilitySetState(durability, D_STATE_DISCOVER_PERSISTENT_SOURCE);
                        os_mutexLock(&listener->masterLock);
                        d_adminAddListener(admin, listener->fellowListener);
                        initMasters(listener);

                        if(store != NULL){
                            initPersistentData(listener);
                        } else {
                            OS_REPORT(OS_WARNING, D_CONTEXT, 0, "Persistency not enabled!");
                        }
                        os_mutexUnlock(&listener->masterLock);
                        d_durabilitySetState(durability, D_STATE_DISCOVER_LOCAL_GROUPS);
                        d_printTimedEvent(durability, D_LEVEL_FINER, D_THREAD_GROUP_LOCAL_LISTENER, "Initializing local groups...\n");

                        if(d_durabilityMustTerminate(durability) == FALSE){
                            u_entityAction(u_entity(dispatcher), d_groupLocalListenerInitLocalGroups, listener);
                        }
                        d_durabilitySetState(durability, D_STATE_FETCH_INITIAL);
                        listener->initialGroupsAdministrated = TRUE;
                        d_printTimedEvent(durability, D_LEVEL_FINER, D_THREAD_GROUP_LOCAL_LISTENER, "Local groups initialized.\n");
                    }
                    if(ur == U_RESULT_OK){
                        d_listener(listener)->attached = TRUE;
                        result = TRUE;
                        d_listenerUnlock(d_listener(listener));
                        u_dispatcherNotify(dispatcher);
                    } else {
                        d_listenerUnlock(d_listener(listener));
                    }
                } else {
                    d_listenerUnlock(d_listener(listener));
                }
            } else {
                d_listenerUnlock(d_listener(listener));
            }
        } else {
            d_listenerUnlock(d_listener(listener));
            result = TRUE;
        }
    }
    return result;
}

c_bool
d_groupLocalListenerStop(
    d_groupLocalListener listener)
{
    c_bool result;
    u_result ur;
    d_admin admin;
    d_subscriber subscriber;
    d_waitset waitset;

    assert(d_listenerIsValid(d_listener(listener), D_GROUP_LOCAL_LISTENER));

    result = FALSE;

    if(listener){
        d_listenerLock(d_listener(listener));

        if(d_listener(listener)->attached == TRUE){
            admin = d_listenerGetAdmin(d_listener(listener));
            d_adminRemoveListener(admin, listener->fellowListener);
            d_eventListenerFree(listener->fellowListener);
            listener->fellowListener = NULL;
            subscriber = d_adminGetSubscriber(admin);
            waitset = d_subscriberGetWaitset(subscriber);
            result = d_waitsetDetach(waitset, listener->waitsetData);

            if(result == TRUE) {
                d_waitsetEntityFree(listener->waitsetData);
                ur = U_RESULT_OK;
            } else {
                ur = U_RESULT_ILL_PARAM;
            }
            if(ur == U_RESULT_OK){
                d_listener(listener)->attached = FALSE;
                result = TRUE;
            }
        } else {
            result = TRUE;
        }
        d_listenerUnlock(d_listener(listener));
    }
    return result;
}

void
d_groupLocalListenerInitLocalGroups(
    v_entity e,
    c_voidp args)
{
    d_listener listener;
    d_admin admin;
    d_subscriber subscriber;
    d_store store;
    d_durability durability;
    d_configuration config;

    listener   = d_listener(args);
    admin      = d_listenerGetAdmin(listener);
    durability = d_adminGetDurability(admin);
    subscriber = d_adminGetSubscriber(admin);
    config     = d_durabilityGetConfiguration(durability);
    store      = d_subscriberGetPersistentStore(subscriber);

    v_serviceFillNewGroups(v_service(e));
    d_groupLocalListenerHandleNewGroupsLocal(e, args);
}

c_ulong
d_groupLocalListenerNewGroupLocalAction(
    u_dispatcher o,
    c_ulong event,
    c_voidp userData)
{
    d_admin admin;
    d_durability durability;
    d_listener listener;

    if (o && event && V_EVENT_NEW_GROUP) {
        if (userData) {
            listener   = d_listener(userData);
            assert(d_listenerIsValid(d_listener(listener), D_GROUP_LOCAL_LISTENER));
            admin = d_listenerGetAdmin(listener);
            durability = d_adminGetDurability(admin);

            u_entityAction(u_entity(d_durabilityGetService(durability)), d_groupLocalListenerHandleNewGroupsLocal, userData);

        }
    }
    return V_EVENT_NEW_GROUP;
}

void
d_groupLocalListenerHandleNewGroupsLocal(
    v_entity entity,
    c_voidp args)
{
    d_listener           listener;
    d_groupLocalListener groupListener;
    v_service            kservice;
    c_iter               groups;
    d_admin              admin;
    v_group              group, group2;
    d_group              dgroup;
    d_durabilityKind     kind;
    d_durability         durability;
    d_publisher          publisher;
    d_subscriber         subscriber;
    d_quality            quality;
    d_configuration      config;
    v_durabilityKind     vkind;
    c_bool               added, attached;
    v_topicQos           qos;
    d_adminStatisticsInfo info;

    listener      = d_listener(args);
    groupListener = d_groupLocalListener(args);
    kservice      = v_service(entity);
    admin         = d_listenerGetAdmin(listener);
    publisher     = d_adminGetPublisher(admin);
    subscriber    = d_adminGetSubscriber(admin);
    durability    = d_adminGetDurability(admin);
    config        = d_durabilityGetConfiguration(durability);
    groups        = v_serviceTakeNewGroups(kservice);

    if (groups) {
        group = v_group(c_iterTakeFirst(groups));

        while (group) {
            if(d_durabilityMustTerminate(durability) == FALSE){
                dgroup = NULL;
                qos = v_topicQosRef(group->topic);
                vkind = qos->durability.kind;
                d_reportLocalGroup(durability, D_THREAD_GROUP_LOCAL_LISTENER, group);

                /* Check durability kind. Only transient, transient local
                 * and persistent groups require actions
                 */
                if(vkind == V_DURABILITY_VOLATILE){
                    d_printTimedEvent(durability, D_LEVEL_WARNING,
                                D_THREAD_GROUP_LOCAL_LISTENER,
                                "Ignoring group %s.%s.\n",
                                v_partitionName(v_groupPartition(group)),
                                v_topicName(v_groupTopic(group)));
                    /*update statistics*/
                    info = d_adminStatisticsInfoNew();
                    info->kind = D_ADMIN_STATISTICS_GROUP;
                    info->groupsKnownVolatileDif += 1;
                    info->groupsIgnoredVolatileDif +=1;
                    d_adminUpdateStatistics(admin, info);
                    d_adminStatisticsInfoFree(info);
                }
                if ((vkind == V_DURABILITY_TRANSIENT) ||
                    (vkind == V_DURABILITY_PERSISTENT) ||
                    (vkind == V_DURABILITY_TRANSIENT_LOCAL)) {
                    d_printTimedEvent(durability, D_LEVEL_FINER,
                            D_THREAD_GROUP_LOCAL_LISTENER,
                            "Wait for services to attach.\n");
                    attached = d_durabilityWaitForAttachToGroup(durability, group);
                    d_printTimedEvent(durability, D_LEVEL_FINE,
                            D_THREAD_GROUP_LOCAL_LISTENER,
                            "Administrating group %s.%s.\n",
                            v_partitionName(v_groupPartition(group)),
                            v_topicName(v_groupTopic(group)));

                    if( (vkind == V_DURABILITY_TRANSIENT) ||
                        (vkind == V_DURABILITY_TRANSIENT_LOCAL)){
                        kind = D_DURABILITY_TRANSIENT;
                    } else {
                        kind = D_DURABILITY_PERSISTENT;
                    }
                    dgroup = d_adminGetLocalGroup(
                                 admin,
                                 v_partitionName(v_groupPartition(group)),
                                 v_topicName(v_groupTopic(group)),
                                 kind);

                    if(!dgroup){
                        quality.seconds = 0;
                        quality.nanoseconds = 0;
                        dgroup = d_groupNew(v_partitionName(v_groupPartition(group)),
                                            v_topicName(v_groupTopic(group)),
                                            kind, D_GROUP_INCOMPLETE, quality);
                        d_groupSetKernelGroup(dgroup, group);

                        if(!attached){
                           d_groupSetPrivate(dgroup, TRUE);
                        }
                        added = d_adminAddLocalGroup(admin, dgroup);

                        if(added == FALSE){
                            d_groupFree(dgroup);
                            dgroup = d_adminGetLocalGroup(
                                         admin,
                                         v_partitionName(v_groupPartition(group)),
                                         v_topicName(v_groupTopic(group)),
                                         kind);
                            if(!attached){
                                d_groupSetPrivate(dgroup, TRUE);
                            }
                        }
                        /*
                        else if(d_groupIsBuiltinGroup(dgroup) == TRUE){
                            v_groupFlush(group);
                        }
                        */
                    } else if(!attached){
                        d_groupSetPrivate(dgroup, TRUE);
                    }

                    if(d_groupGetCompleteness(dgroup) != D_GROUP_COMPLETE) {
                        group2 = d_groupGetKernelGroup(dgroup);

                        if(group2){
                            c_free(group2);
                        } else {
                            d_groupSetKernelGroup(dgroup, group);
                        }
                        if(d_durabilityMustTerminate(durability) == FALSE){
                            d_groupLocalListenerHandleAlignment(groupListener, dgroup, NULL);
                        }
                    } else {
                        d_printTimedEvent(durability, D_LEVEL_FINE,
                            D_THREAD_GROUP_LOCAL_LISTENER,
                            "Group %s.%s already complete.\n",
                            v_partitionName(v_groupPartition(group)),
                            v_topicName(v_groupTopic(group)));
                    }
                }
                c_free(group);
                group = (v_group)c_iterTakeFirst(groups);
            } else {
                c_free(group);
                group = (v_group)c_iterTakeFirst(groups);

                while(group){
                    c_free(group);
                    group = (v_group)c_iterTakeFirst(groups);
                }
            }
        }
        c_iterFree(groups);
    }
}

void
d_groupLocalListenerHandleAlignment(
    d_groupLocalListener listener,
    d_group dgroup,
    d_readerRequest readerRequest)
{
    d_timestamp         stamp, zeroTime, networkAttachTime;
    d_sampleRequest     request;
    d_admin             admin;
    d_durability        durability;
    d_subscriber        subscriber;
    d_store             store;
    d_completeness      completeness;
    d_configuration     config;
    d_durabilityKind    dkind, nsdkind;
    c_bool              timeRangeActive, requestRemote, inject;
    d_group             localGroup;
    c_char              *partition, *topic;
    d_nameSpace         nameSpace;
    d_alignmentKind     akind;
    d_storeResult       result;
    d_chain             chain;
    d_adminStatisticsInfo info;

    assert(d_listenerIsValid(d_listener(listener), D_GROUP_LOCAL_LISTENER));

    admin      = d_listenerGetAdmin(d_listener(listener));
    subscriber = d_adminGetSubscriber(admin);
    store      = d_subscriberGetPersistentStore(subscriber);
    durability = d_adminGetDurability(admin);
    config     = d_durabilityGetConfiguration(durability);
    partition  = d_groupGetPartition(dgroup);
    topic      = d_groupGetTopic(dgroup);
    localGroup = d_adminGetLocalGroup(admin, partition, topic, d_groupGetKind(dgroup));

    if(localGroup){
        completeness = d_groupGetCompleteness(localGroup);
        dkind        = d_groupGetKind(localGroup);

        if(readerRequest){
            requestRemote = FALSE;
            nameSpace = d_configurationGetNameSpaceForGroup(config, partition, topic, dkind);

            if(nameSpace){
                akind = d_nameSpaceGetAlignmentKind(nameSpace);

                if((akind == D_ALIGNEE_ON_REQUEST) && (completeness != D_GROUP_COMPLETE)){
                    if(dkind == D_DURABILITY_PERSISTENT){
                        inject = d_nameSpaceMasterIsMe(nameSpace, admin);

                        if(inject || d_groupIsPrivate(localGroup)){
                            result = d_storeMessagesInject(store, localGroup);
                            d_readerRequestRemoveGroup(readerRequest, localGroup);
                        } else {
                            requestRemote = TRUE;
                        }
                    } else if(!d_groupIsPrivate(localGroup)){
                        requestRemote = TRUE;
                    } else {
                        /* All data available, so we're done for this group*/
                    }

                } else if(completeness == D_GROUP_COMPLETE){
                    d_readerRequestRemoveGroup(readerRequest, localGroup);
                } else {
                    /* Simply wait for group completeness later on */
                }
            } else {
                /*Not in nameSpace so ignore request for this group*/
                d_readerRequestRemoveGroup(readerRequest, localGroup);

                d_printTimedEvent(durability, D_LEVEL_WARNING,
                    D_THREAD_GROUP_LOCAL_LISTENER,
                    "Received a historicalDataRequest from a reader for group %s.%s, "\
                    "but that is not in the nameSpace and therefore ignored.\n",
                    partition, topic);

            }

            if(requestRemote == TRUE){
                stamp = v_timeGet();

                if(config->timeAlignment){
                    networkAttachTime    = stamp;
                } else {
                    networkAttachTime = C_TIME_INFINITE;
                }
                timeRangeActive      = FALSE;
                zeroTime.seconds     = 0;
                zeroTime.nanoseconds = 0;

                request = d_sampleRequestNew(
                                admin, partition, topic,
                                dkind, stamp, timeRangeActive,
                                zeroTime, networkAttachTime);

                d_sampleRequestSetCondition(request, readerRequest);
                chain = d_chainNew(admin, request);
                d_readerRequestAddChain(readerRequest, chain);
                d_readerRequestRemoveGroup(readerRequest, localGroup);

                d_sampleChainListenerInsertRequest(
                            d_groupLocalListener(listener)->sampleChainListener,
                            chain, TRUE);

            }
        } else if(d_configurationGroupInActiveAligneeNS(config, partition, topic, dkind) == TRUE){
            if(completeness != D_GROUP_COMPLETE){
                if(dkind == D_DURABILITY_PERSISTENT){
                    nameSpace = d_configurationGetNameSpaceForGroup(config, partition, topic, dkind);
                    assert(nameSpace);
                    akind     = d_nameSpaceGetAlignmentKind(nameSpace);
                    inject    = d_nameSpaceMasterIsMe(nameSpace, admin);

                    if(inject == TRUE){
                        nsdkind = d_nameSpaceGetDurabilityKind(nameSpace);

                        if( (nsdkind == D_DURABILITY_ALL) ||
                            (nsdkind == D_DURABILITY_PERSISTENT)
                        ){
                            d_storeGroupStore(store, localGroup);
                        }
                        if(akind == D_ALIGNEE_LAZY){
                            result = d_storeMessagesInject(store, localGroup);

                            if(result == D_STORE_RESULT_OK){
                                d_printTimedEvent(durability, D_LEVEL_FINE,
                                    D_THREAD_GROUP_LOCAL_LISTENER,
                                    "Data for group %s.%s injected from disk. Group is complete now (1).\n",
                                    partition, topic);
                            } else {
                                d_printTimedEvent(durability, D_LEVEL_SEVERE,
                                D_THREAD_GROUP_LOCAL_LISTENER,
                                "Injecting data from disk for group %s.%s failed (1).\n",
                                partition, topic);
                            }
                        } else if(akind == D_ALIGNEE_ON_REQUEST){
                            result = d_storeMessagesInject(store, localGroup);

                            if(result == D_STORE_RESULT_OK){
                                d_printTimedEvent(durability, D_LEVEL_FINE,
                                    D_THREAD_GROUP_LOCAL_LISTENER,
                                    "Data for group %s.%s injected from disk. Group is complete now (2).\n",
                                    partition, topic);
                            } else {
                                d_printTimedEvent(durability, D_LEVEL_SEVERE,
                                D_THREAD_GROUP_LOCAL_LISTENER,
                                "Injecting data from disk for group %s.%s failed (1).\n",
                                partition, topic);
                            }
                        } else if(akind == D_ALIGNEE_INITIAL_AND_ALIGNER){
                            d_printTimedEvent(durability, D_LEVEL_FINE,
                                D_THREAD_GROUP_LOCAL_LISTENER,
                                "Persistent group %s.%s complete, because I am persistent source and already injected data (1).\n",
                                partition, topic);
                            result = D_STORE_RESULT_OK;
                        } else if(akind == D_ALIGNEE_INITIAL){
                            d_printTimedEvent(durability, D_LEVEL_FINE,
                                D_THREAD_GROUP_LOCAL_LISTENER,
                                "Persistent group %s.%s complete, because I am persistent source and already injected data (2).\n",
                                partition, topic);
                            result = D_STORE_RESULT_OK;
                        } else {
                            assert(FALSE);
                        }
                        if(d_groupIsPrivate(localGroup)){
                            d_groupSetComplete(localGroup);
                            requestRemote = FALSE;
                        } else {
                            requestRemote = TRUE;
                        }
                    } else if(d_groupIsPrivate(localGroup)){
                        /* I am not the master, but this is a persistent group
                         * that exists on this node only, so inject data for
                         * group.
                         */
                        result = d_storeMessagesInject(store, localGroup);

                        if(result == D_STORE_RESULT_OK){
                            d_printTimedEvent(durability, D_LEVEL_FINE,
                                D_THREAD_GROUP_LOCAL_LISTENER,
                                "Data for local group %s.%s injected from disk. Group is complete now (3).\n",
                                partition, topic);
                        } else {
                            d_printTimedEvent(durability, D_LEVEL_SEVERE,
                                D_THREAD_GROUP_LOCAL_LISTENER,
                                "Injecting data from disk for local group %s.%s failed (3).\n",
                                partition, topic);
                        }
                        d_groupSetComplete(localGroup);
                        requestRemote = FALSE;
                    } else {
                        /*Only request if complete.*/
                        d_storeGroupStore(store, localGroup);
                        requestRemote = TRUE;
                    }
                } else if(d_groupIsPrivate(localGroup)){
                    /* This group is private and transient so it is complete */
                    d_groupSetComplete(localGroup);
                    d_printTimedEvent(durability, D_LEVEL_FINE,
                        D_THREAD_GROUP_LOCAL_LISTENER,
                        "Transient group %s.%s complete, because it is local.\n",
                        partition, topic);
                    requestRemote = FALSE;
                } else {
                    requestRemote = TRUE;
                }

                if(requestRemote == TRUE){
                    stamp = v_timeGet();

                    if(config->timeAlignment){
                        networkAttachTime    = stamp;
                    } else {
                        networkAttachTime = C_TIME_INFINITE;
                    }
                    timeRangeActive      = FALSE;
                    zeroTime.seconds     = 0;
                    zeroTime.nanoseconds = 0;

                    if((dkind == D_DURABILITY_PERSISTENT) && store){
                        result = d_storeGroupStore(store, localGroup);

                        if(result == D_STORE_RESULT_OK){
                            d_printTimedEvent(durability, D_LEVEL_FINE,
                                D_THREAD_GROUP_LOCAL_LISTENER,
                                "Persistent group %s.%s stored on disk.\n",
                                partition, topic);
                        } else {
                            d_printTimedEvent(durability, D_LEVEL_FINE,
                                D_THREAD_GROUP_LOCAL_LISTENER,
                                "Storing persistent group %s.%s on disk failed (error code: %d).\n",
                                partition, topic, result);
                        }
                    }
                    request = d_sampleRequestNew(admin, partition, topic,
                            dkind, stamp, timeRangeActive, zeroTime, networkAttachTime);
                    chain   = d_chainNew(admin, request);
                    d_sampleChainListenerInsertRequest(
                                d_groupLocalListener(listener)->sampleChainListener,
                                chain, TRUE);
                }
            }
        } else if(d_configurationGroupInAligneeNS(config, partition, topic, dkind) == TRUE){
            d_sampleChainListenerReportGroup(listener->sampleChainListener, localGroup);
        } else {
            d_printTimedEvent(durability, D_LEVEL_FINE,
                            D_THREAD_GROUP_LOCAL_LISTENER,
                            "Group %s.%s not in alignee namespace, so no alignment action taken.\n",
                            partition, topic);
            /*update statistics*/
            info = d_adminStatisticsInfoNew();
            info->kind = D_ADMIN_STATISTICS_GROUP;

            switch(dkind){
            case D_DURABILITY_VOLATILE:
                info->groupsIncompleteVolatileDif -= 1;
                info->groupsIgnoredVolatileDif +=1;
                break;
            case D_DURABILITY_TRANSIENT_LOCAL:
            case D_DURABILITY_TRANSIENT:
                info->groupsIncompleteTransientDif -= 1;
                info->groupsIgnoredTransientDif +=1;
                break;
            case D_DURABILITY_PERSISTENT:
                info->groupsIncompletePersistentDif -= 1;
                info->groupsIgnoredPersistentDif +=1;
                break;
            default:
                break;
            }

            d_adminUpdateStatistics(admin, info);
            d_adminStatisticsInfoFree(info);
        }
    }
    os_free(partition);
    os_free(topic);
}
