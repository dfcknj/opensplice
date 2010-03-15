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
#include "cmx_entity.h"
#include "cmx__entity.h"
#include "cmx__factory.h"
#include "cmx__participant.h"
#include "cmx__service.h"
#include "cmx__serviceState.h"
#include "cmx__publisher.h"
#include "cmx__subscriber.h"
#include "cmx__writer.h"
#include "cmx__query.h"
#include "cmx__domain.h"
#include "cmx__reader.h"
#include "cmx__topic.h"
#include "cmx__waitset.h"
#include "cmx_factory.h"
#include "cmx__qos.h"
#include "sd_serializer.h"
#include "sd_serializerXML.h"
#include "u_entity.h"
#include "u__entity.h"
#include "u_participant.h"
#include "u__participant.h"
#include "u_service.h"
#include "u_dataReader.h"
#include "u_networkReader.h"
#include "u_topic.h"
#include "u_groupQueue.h"
#include "u_partition.h"
#include "u_query.h"
#include "u_publisher.h"
#include "u_subscriber.h"
#include "v_kernel.h"
#include "v_entity.h"
#include "v_statistics.h"
#include "c_typebase.h"
#include "v_public.h"
#include "v_qos.h"
#include "os.h"
#include "sd_serializer.h"
#include <os_abstract.h>
#include <stdio.h>

void
cmx_entityNewFromAction(
    v_entity entity,
    c_voidp args)
{
    cmx_entityNewFromWalk(entity, args);
}

c_bool
cmx_entityNewFromWalk(
    v_entity entity,
    c_voidp args)
{
    char buf[1024];
    c_char* special;
    const c_char* enabled;
    cmx_entityArg arg;
    u_entity proxy;
    c_bool result;
        
    arg = cmx_entityArg(args);
    if(arg->create == TRUE){
        proxy = u_entityNew(entity, arg->participant, FALSE);
        cmx_registerEntity(proxy);
    } else {
        proxy = arg->entity;
        
    }
    if((proxy != NULL) || (arg->create == FALSE)){
        special = cmx_entityInit(entity, proxy, arg->create);

        if(special != NULL){
            if(entity->enabled){
                enabled = "TRUE";
            } else {
                enabled = "FALSE";
            }
            if(entity->name != NULL){
                sprintf(buf, "<entity><pointer>"PA_ADDRFMT"</pointer><handle_index>%d</handle_index><handle_serial>%d</handle_serial><name>%s</name><enabled>%s</enabled>%s</entity>",
                            (c_address)proxy,
                            v_public(entity)->handle.index,
                            v_public(entity)->handle.serial,
                            entity->name,
                            enabled,
                            special);
            }
            else{
                sprintf(buf, "<entity><pointer>"PA_ADDRFMT"</pointer><handle_index>%d</handle_index><handle_serial>%d</handle_serial><name>NULL</name><enabled>%s</enabled>%s</entity>",
                        (c_address)proxy,
                        v_public(entity)->handle.index,
                        v_public(entity)->handle.serial,
                        enabled,
                        special);
            }   
            os_free(special);
            arg->result = (c_voidp)(os_strdup(buf));
            result = TRUE;
        } else {
            result = FALSE;
        }
    } else {
        result = FALSE;
    }
    return result;
}

c_char*
cmx_entityInit(
    v_entity entity,
    u_entity proxy,
    c_bool init)
{
    c_char* result;
    u_kernel uk;
    u_participant up;
    u_result ur;
    
    ur = U_RESULT_OK;
    result = NULL;
    
    if((proxy == NULL) && (init == TRUE)){
        ur = U_RESULT_ILL_PARAM;
    } else if(proxy != NULL){
        up = u_entityParticipant(proxy);
        
        if(up != NULL){
            uk = u_participantKernel(up);
        
            if(uk == NULL){
                ur = U_RESULT_ILL_PARAM;
                OS_REPORT(OS_ERROR, CM_XML_CONTEXT, 0, 
                          "cmx_entityInit proxy == NULL && init == TRUE "
                          "but proxy participant has no kernel.");
            }
        } else {
            OS_REPORT(OS_ERROR, CM_XML_CONTEXT, 0, 
                "cmx_entityInit proxy == NULL && init == TRUE "
                "but proxy has no participant.");
            ur = U_RESULT_ILL_PARAM;
        }
    }
    
    if(ur == U_RESULT_OK){
        switch(v_object(entity)->kind){
        case K_PARTICIPANT:    
            result = cmx_participantInit((v_participant)entity);    
        break;
        case K_NETWORKING:
        case K_DURABILITY:
        case K_CMSOAP:
        case K_SPLICED:
            /*fallthrough on purpose.*/
        case K_SERVICE:         
            result = cmx_serviceInit((v_service)entity);            
        break;
        case K_PUBLISHER:
            result = cmx_publisherInit((v_publisher)entity);        
        break;
        case K_SERVICESTATE:
            result = cmx_serviceStateInit((v_serviceState)entity);  
        break;
        case K_SUBSCRIBER:  
            result = cmx_subscriberInit((v_subscriber)entity);      
        break;
        case K_WRITER:          
            result = cmx_writerInit((v_writer)entity);
        break;
        case K_QUERY:      
            /*fallthrough on purpose.*/
        case K_DATAREADERQUERY:     
            result = cmx_queryInit((v_query)entity);                
        break;
        case K_DOMAIN:          
            result = cmx_domainInit((v_partition)entity);              
        break;
        case K_NETWORKREADER: 
            /*fallthrough on purpose.*/
        case K_DATAREADER:      
            /*fallthrough on purpose.*/
        case K_DELIVERYSERVICE:      
            /*fallthrough on purpose.*/
        case K_GROUPQUEUE:      
            result = cmx_readerInit((v_reader)entity);              
        break;
        case K_TOPIC:
            result = cmx_topicInit((v_topic)entity);    
        break;
        case K_WAITSET:           
            result = cmx_waitsetInit((v_waitset)entity);    
        break;
        default:
            OS_REPORT_1(OS_ERROR, CM_XML_CONTEXT, 0, 
                        "Unknown entity kind: '%d'\n",
                        v_object(entity)->kind);
            assert(FALSE);                                          
        break;
        }
    } else {
        OS_REPORT(OS_ERROR, CM_XML_CONTEXT, 0, 
                  "cmx_entityInit ur != U_RESULT_OK.");
        cmx_deregisterEntity(proxy);
        assert(FALSE);
    }
    return result;
}



void
cmx_entityFree(
    c_char* entity)
{
    u_entity e;
    u_entity e2;
    
    if(entity != NULL){
        e = cmx_entityUserEntity(entity);
        
        if(e != NULL){
            e2 = cmx_deregisterEntity(e);
            
            if(e2 != NULL){
                cmx_entityFreeUserEntity(e2);
            }
        }
        os_free(entity);
    }
}

void
cmx_entityFreeUserEntity(
    u_entity entity)
{   
    cmx_entityKindArg arg;
    
    if(entity != NULL){
        if(u_entityOwner(entity) == FALSE){
            u_entityFree(entity);
        } else {
            /*find kind and call entity specific free.*/
            arg = cmx_entityKindArg(os_malloc(C_SIZEOF(cmx_entityKindArg)));
            u_entityAction(entity, cmx_entityKindAction, (c_voidp)arg);
            
            switch(arg->kind){
            case K_PARTICIPANT:
                u_participantFree(u_participant(entity));
                break;
            case K_PUBLISHER:
                u_publisherFree(u_publisher(entity));
                break;
            case K_SUBSCRIBER:
                u_subscriberFree(u_subscriber(entity));
                break;
            case K_GROUPQUEUE:
                u_groupQueueFree(u_groupQueue(entity));
                break;
            case K_DATAREADER:
                u_dataReaderFree(u_dataReader(entity));
                break;
            case K_DOMAIN:
                u_partitionFree(u_partition(entity));
                break;
            case K_QUERY:
            case K_DATAREADERQUERY:
                u_queryFree(u_query(entity));
                break;
            case K_TOPIC:
                u_topicFree(u_topic(entity));
                break;
            case K_WRITER:
                u_writerFree(u_writer(entity));
                break;
            case K_WAITSET:
                u_waitsetFree(u_waitset(entity));
                break;
            default:
                OS_REPORT(OS_WARNING, CM_XML_CONTEXT, 0,
                          "entity already freed.\n");
                break;
            }
            os_free(arg);
        }
    }
}

void
cmx_entityKindAction(
    v_entity entity,
    c_voidp args)
{
    cmx_entityKindArg arg;
    
    arg = cmx_entityKindArg(args);
    arg->kind = v_object(entity)->kind;
}

c_bool
cmx_entityWalkAction(
    v_entity e,
    c_voidp args)
{
  cmx_walkEntityArg arg;
  c_char* xmlEntity;
  c_bool add;
  c_bool proceed;
  
  arg = cmx_walkEntityArg(args);
  add = FALSE;
  
  if(e != NULL){
      switch(arg->filter){
        /*User filter, select all entities of the supplied+inherited kinds */
        case K_ENTITY:/*Always add the entity.*/
            add = TRUE;
        break;
        case K_QUERY:
            switch(v_object(e)->kind){
            case K_QUERY:       
            case K_DATAREADERQUERY:       
                                add = TRUE; break;  
            default:            break;
            }
        break;
        case K_TOPIC:
            switch(v_object(e)->kind){
            case K_TOPIC:       add = TRUE;  break;  
            default:            break;
            }
        break;
        case K_PUBLISHER:
            switch(v_object(e)->kind){
            case K_PUBLISHER:   add = TRUE;  break;  
            default:            break;
            }
        break; 
        case K_SUBSCRIBER:
            switch(v_object(e)->kind){
            case K_SUBSCRIBER:  add = TRUE;  break;  
            default:            break;
            }
        break;
        case K_DOMAIN:
            switch(v_object(e)->kind){
            case K_DOMAIN:      add = TRUE;  break;  
            default:            break;
            }
        break; 
        case K_READER:
            switch(v_object(e)->kind){
            case K_READER:
            case K_DATAREADER:
            case K_NETWORKREADER:
            case K_GROUPQUEUE:
            case K_QUERY:       
            case K_DATAREADERQUERY:
                                add = TRUE;  break;  
            default:            break;
            }
        break; 
        case K_DATAREADER:
            switch(v_object(e)->kind){
            case K_DATAREADER:  add = TRUE;  break;  
            default:            break;
            }
        break; 
        case K_GROUPQUEUE:
            switch(v_object(e)->kind){
            case K_GROUPQUEUE:  add = TRUE;  break;  
            default:            break;
            }
        break; 
        case K_NETWORKREADER:
            switch(v_object(e)->kind){
            case K_NETWORKREADER:   add = TRUE;  break;  
            default:                break;
            }
        break; 
        case K_WRITER:
            switch(v_object(e)->kind){
            case K_WRITER:      add = TRUE;  break;  
            default:            break;
            }
        break; 
        case K_PARTICIPANT:
            switch(v_object(e)->kind){
            case K_PARTICIPANT:
            case K_SERVICE:
            case K_SPLICED:
            case K_NETWORKING:
            case K_DURABILITY:
            case K_CMSOAP:
                                add = TRUE;  break;  
            default:            break;
            }
        break;  
        case K_SERVICE:
            switch(v_object(e)->kind){
            case K_SERVICE:
            case K_SPLICED:
            case K_NETWORKING:
            case K_DURABILITY:
            case K_CMSOAP:     
                                add = TRUE;  break;  
            default:            break;
            }
        break; 
        case K_SERVICESTATE:
            switch(v_object(e)->kind){
            case K_SERVICESTATE:add = TRUE;  break;  
            default:            break;
            }
        break; 
        case K_WAITSET:
            switch(v_object(e)->kind){
            case K_WAITSET: add = TRUE;  break;  
            default:            break;
            }
        break; 
        default: 
            OS_REPORT_1(OS_ERROR, CM_XML_CONTEXT, 0,
                        "Unknown Entity found in cmx_entityWalkAction: %d\n",
                        v_object(e)->kind);
        break;
        }
    }
    if(add == TRUE){
        proceed = cmx_entityNewFromWalk(e, arg->entityArg);
        if(proceed == TRUE){
            xmlEntity = arg->entityArg->result;
            
            if(xmlEntity == NULL){
                OS_REPORT_1(OS_ERROR, CM_XML_CONTEXT, 0,
                            "Entity found: %d\n",
                            v_object(e)->kind);
                assert(FALSE);
            } else {
                arg->list = c_iterInsert(arg->list, xmlEntity);
                arg->length += strlen(xmlEntity);
            }
        }
    }
    return TRUE; 
}

c_char *
cmx_entityOwnedEntities(
    const c_char* entity,
    const c_char* filter)
{
    u_entity e;
    c_char* result;
    v_kind kind;
    u_result walkSuccess;
    cmx_walkEntityArg arg;
    
    result = NULL;
    e = cmx_entityUserEntity(entity);
    kind = cmx_resolveKind(filter);
        
    arg = cmx_walkEntityArg(os_malloc(C_SIZEOF(cmx_walkEntityArg)));
    arg->length = 0;
    arg->filter = kind;
    arg->list = NULL;
    
    arg->entityArg = cmx_entityArg(os_malloc(C_SIZEOF(cmx_entityArg)));
    arg->entityArg->participant = u_entityParticipant(e);
    arg->entityArg->create = TRUE;
    arg->entityArg->result = NULL;
    
    walkSuccess = u_entityWalkEntities(e, cmx_entityWalkAction, (c_voidp)(arg));
    
    if(walkSuccess == U_RESULT_OK){
        result = cmx_convertToXMLList(arg->list, arg->length);
    }
    os_free(arg->entityArg);
    os_free(arg);

    return result;
}

c_char *
cmx_entityDependantEntities(
    const c_char* entity,
    const c_char* filter)
{
    u_entity e;
    c_char* result;
    v_kind kind;
    u_result walkSuccess;
    cmx_walkEntityArg arg;
    
    result = NULL;
    e = cmx_entityUserEntity(entity);
    kind = cmx_resolveKind(filter);
        
    arg = cmx_walkEntityArg(os_malloc(C_SIZEOF(cmx_walkEntityArg)));
    arg->length = 0;
    arg->filter = kind;
    arg->list = NULL;
    
    arg->entityArg = cmx_entityArg(os_malloc(C_SIZEOF(cmx_entityArg)));
    arg->entityArg->participant = u_entityParticipant(e);
    arg->entityArg->create = TRUE;
    arg->entityArg->result = NULL;
    
    walkSuccess = u_entityWalkDependantEntities(e,
                                                cmx_entityWalkAction,
                                                (c_voidp)(arg));   
    
    if(walkSuccess == U_RESULT_OK){
        result = cmx_convertToXMLList(arg->list, arg->length);
    }
    os_free(arg->entityArg);
    os_free(arg);
    
    return result;
}

struct cmx_statusArg {
    c_char* result;
};

c_char*
cmx_entityStatus(
    const c_char* entity)
{
    u_entity e;
    c_bool actionResult;
    struct cmx_statusArg arg;
    
    arg.result = NULL;

    e = cmx_entityUserEntity(entity);
    
    if(e != NULL){
        actionResult = u_entityAction(e, cmx_entityStatusAction, &arg);    
    }
    return arg.result;
}

void
cmx_entityStatusAction(
    v_entity entity,
    c_voidp args)
{
    sd_serializer ser;
    sd_serializedData data;
    struct cmx_statusArg *arg;
    arg = (struct cmx_statusArg *)args;
    
    ser = sd_serializerXMLNewTyped(c_getType((c_object)entity->status));
    data = sd_serializerSerialize(ser, entity->status);
    arg->result = sd_serializerToString(ser, data);
    sd_serializedDataFree(data);
    sd_serializerFree(ser);
}

struct cmx_statisticsArg {
    c_char* result;
};

c_char*
cmx_entityStatistics(
    const c_char* entity)
{
    u_entity e;
    u_result actionResult;
    struct cmx_statisticsArg arg;
    
    arg.result = NULL;

    e = cmx_entityUserEntity(entity);
    
    if(e != NULL){
        actionResult = u_entityAction(e, cmx_entityStatisticsAction, &arg);    
    }
    return arg.result;
}

void
cmx_entityStatisticsAction(
    v_entity entity,
    c_voidp args)
{
    sd_serializer ser;
    sd_serializedData data;
    struct cmx_statisticsArg *arg;
    arg = (struct cmx_statisticsArg *)args;
    
    if(entity->statistics != NULL){
        ser = sd_serializerXMLNewTyped(c_getType((c_object)entity->statistics));
        data = sd_serializerSerialize(ser, entity->statistics);
        arg->result = sd_serializerToString(ser, data);
        sd_serializedDataFree(data);
        sd_serializerFree(ser);
    }
}

struct cmx_resetStatisticsArg {
    const c_char* fieldName;
    const c_char* result;
};

const c_char*
cmx_entityResetStatistics(
    const c_char* entity,
    const c_char* fieldName)
{
    u_entity e;
    struct cmx_resetStatisticsArg arg;
    u_result actionResult;
    
    e = cmx_entityUserEntity(entity);
    arg.result = CMX_RESULT_ENTITY_NOT_AVAILABLE;
    
    if(e != NULL){
        arg.fieldName = fieldName;
        
        actionResult = u_entityAction(e, cmx_entityStatisticsResetAction, &arg);
    }
    return arg.result;
}

void
cmx_entityStatisticsResetAction(
    v_entity entity,
    c_voidp args)
{
    struct cmx_resetStatisticsArg *arg;
    c_bool result;
    arg = (struct cmx_resetStatisticsArg *)args;
        
    if(entity->statistics != NULL){
        result = v_statisticsReset(entity->statistics, arg->fieldName);
        
        if(result == TRUE){
            arg->result = CMX_RESULT_OK;
        } else {
            arg->result = CMX_RESULT_FAILED;
        }
    }
}

c_char*
cmx_entityQoS(
    const c_char* entity)
{
    c_char* result;
    u_result ur;
    u_entity e;
    
    result = NULL;
    e = cmx_entityUserEntity(entity);

    if (e != NULL) {
        ur = u_entityAction(e, (void (*)(v_entity e, c_voidp arg))cmx_qosNew, &result);
    }
    return result;
}

const c_char*
cmx_entitySetQoS(
    const c_char* entity,
    const c_char* qos)
{
    v_qos vqos;
    u_entity e;
    u_result ur;
    const c_char* result;
    
    result = CMX_RESULT_FAILED;
    vqos = cmx_qosKernelQos(entity, qos);
    
    if(vqos != NULL){
        e = cmx_entityUserEntity(entity);
        
        if(e != NULL){
            ur = u_entitySetQoS(e, vqos);
            
            if(ur == U_RESULT_OK){
                result = CMX_RESULT_OK;
            } else if(ur == U_RESULT_ILL_PARAM){
                result = CMX_RESULT_ILL_PARAM;
            } else if(ur == U_RESULT_INCONSISTENT_QOS){
                result = CMX_RESULT_INCONSISTENT_QOS;
            } else if(ur == U_RESULT_IMMUTABLE_POLICY){
                result = CMX_RESULT_IMMUTABLE_POLICY;
            } else {
                result = CMX_RESULT_FAILED;
            }
        }
        c_free(vqos);
    } 
    return result;
}

const c_char*
cmx_entityEnable(
    const c_char* entity)
{
    u_entity e;
    const c_char* result;
    
    result = CMX_RESULT_ENTITY_NOT_AVAILABLE;    
    e = cmx_entityUserEntity(entity);
        
    if(e != NULL){
        result = CMX_RESULT_NOT_IMPLEMENTED;
    }
    return result;
}

u_entity
cmx_entityUserEntity(
    const c_char* xmlEntity)
{
    c_char* copy;
    c_char* temp;
    u_entity e;
    
    e = NULL;
    
    if(xmlEntity != NULL){
        if(cmx_isInitialized() == TRUE){
            copy = (c_char*)(os_malloc(strlen(xmlEntity) + 1));
            strcpy(copy, xmlEntity);
            temp = strtok((c_char*)copy, "</>");    /*<entity>*/
            temp = strtok(NULL, "</>");             /*<pointer>*/
            temp = strtok(NULL, "</>");             /*... the pointer*/
             
            if(temp != NULL){
                sscanf(temp, PA_ADDRFMT, (c_address *)(&e));
            }
            os_free(copy);
        } else {
            cmx_detach();
        }
    }
    return e;
}

void
cmx_entityKernelAction(
    v_entity entity,
    c_voidp args)
{
    cmx_entityKernelArg arg;
    
    arg = cmx_entityKernelArg(args);
    
    if(entity != NULL){
        arg->kernel = v_objectKernel(entity);
    }
}
