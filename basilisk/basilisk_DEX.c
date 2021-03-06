/******************************************************************************
 * Copyright © 2014-2016 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

// included from basilisk.c
// requestid is invariant for a specific request
// quoteid is invariant for a specific request after dest fields are set

int32_t basilisk_rwDEXquote(int32_t rwflag,uint8_t *serialized,struct basilisk_request *rp)
{
    int32_t len = 0;
    len += iguana_rwnum(rwflag,&serialized[len],sizeof(rp->requestid),&rp->requestid);
    len += iguana_rwnum(rwflag,&serialized[len],sizeof(rp->timestamp),&rp->timestamp); // must be 2nd
    len += iguana_rwnum(rwflag,&serialized[len],sizeof(rp->quoteid),&rp->quoteid);
    len += iguana_rwnum(rwflag,&serialized[len],sizeof(rp->quotetime),&rp->quotetime);
    len += iguana_rwnum(rwflag,&serialized[len],sizeof(rp->relaybits),&rp->relaybits);
    len += iguana_rwnum(rwflag,&serialized[len],sizeof(rp->srcamount),&rp->srcamount);
    len += iguana_rwnum(rwflag,&serialized[len],sizeof(rp->minamount),&rp->minamount);
    len += iguana_rwbignum(rwflag,&serialized[len],sizeof(rp->hash),rp->hash.bytes);
    len += iguana_rwbignum(rwflag,&serialized[len],sizeof(rp->desthash),rp->desthash.bytes);
    len += iguana_rwnum(rwflag,&serialized[len],sizeof(rp->destamount),&rp->destamount);
    if ( rwflag != 0 )
    {
        memcpy(&serialized[len],rp->src,sizeof(rp->src)), len += sizeof(rp->src);
        memcpy(&serialized[len],rp->dest,sizeof(rp->dest)), len += sizeof(rp->dest);
    }
    else
    {
        memcpy(rp->src,&serialized[len],sizeof(rp->src)), len += sizeof(rp->src);
        memcpy(rp->dest,&serialized[len],sizeof(rp->dest)), len += sizeof(rp->dest);
    }
    if ( rp->quoteid != 0 && basilisk_quoteid(rp) != rp->quoteid )
        printf("basilisk_rwDEXquote.%d: quoteid.%u mismatch calc %u\n",rwflag,rp->quoteid,basilisk_quoteid(rp));
    if ( basilisk_requestid(rp) != rp->requestid )
        printf("basilisk_rwDEXquote.%d: requestid.%u mismatch calc %u\n",rwflag,rp->requestid,basilisk_requestid(rp));
    return(len);
}

uint32_t basilisk_request_enqueue(struct supernet_info *myinfo,struct basilisk_request *rp)
{
    uint8_t serialized[256]; int32_t len; struct queueitem *item;
    len = basilisk_rwDEXquote(1,serialized+1,rp);
    if ( (item= calloc(1,sizeof(*item) + len + 1)) != 0 )
    {
        serialized[0] = len;
        memcpy(&item[1],serialized,len + 1);
        portable_mutex_lock(&myinfo->DEX_mutex);
        DL_APPEND(myinfo->DEX_quotes,item);
        portable_mutex_unlock(&myinfo->DEX_mutex);
        printf("ENQUEUE.%u calc.%u\n",rp->requestid,basilisk_requestid(rp));
        return(rp->requestid);
    }
    return(0);
}

cJSON *basilisk_requestjson(struct basilisk_request *rp)
{
    char ipaddr[64]; cJSON *item = cJSON_CreateObject();
    if ( rp->relaybits != 0 )
    {
        expand_ipbits(ipaddr,rp->relaybits);
        jaddstr(item,"relay",ipaddr);
    }
    jaddbits256(item,"hash",rp->hash);
    if ( bits256_nonz(rp->desthash) != 0 )
        jaddbits256(item,"desthash",rp->desthash);
    jaddstr(item,"src",rp->src);
    if ( rp->srcamount != 0 )
        jadd64bits(item,"srcamount",rp->srcamount);
    if ( rp->minamount != 0 )
        jadd64bits(item,"minamount",rp->minamount);
    jaddstr(item,"dest",rp->dest);
    if ( rp->destamount != 0 )
        jadd64bits(item,"destamount",rp->destamount);
    jaddnum(item,"quotetime",rp->quotetime);
    jaddnum(item,"timestamp",rp->timestamp);
    jaddnum(item,"requestid",rp->requestid);
    jaddnum(item,"quoteid",rp->quoteid);
    if ( rp->quoteid != 0 && basilisk_quoteid(rp) != rp->quoteid )
        printf("quoteid mismatch %u vs %u\n",basilisk_quoteid(rp),rp->quoteid);
    if ( basilisk_requestid(rp) != rp->requestid )
        printf("requestid mismatch %u vs calc %u\n",rp->requestid,basilisk_requestid(rp));
    {
        int32_t i; struct basilisk_request R;
        if ( basilisk_parsejson(&R,item) != 0 )
        {
            if ( memcmp(&R,rp,sizeof(*rp)) != 0 )
            {
                for (i=0; i<sizeof(*rp); i++)
                    printf("%02x",((uint8_t *)rp)[i]);
                printf(" <- rp\n");
                for (i=0; i<sizeof(R); i++)
                    printf("%02x",((uint8_t *)&R)[i]);
                printf(" <- R mismatch\n");
                for (i=0; i<sizeof(R); i++)
                    if ( ((uint8_t *)rp)[i] != ((uint8_t *)&R)[i] )
                        printf("(%02x %02x).%d ",((uint8_t *)rp)[i],((uint8_t *)&R)[i],i);
                printf("mismatches\n");
            } //else printf("matched JSON conv %u %u\n",basilisk_requestid(&R),basilisk_requestid(rp));
        }
    }
    return(item);
}

int32_t basilisk_request_create(struct basilisk_request *rp,cJSON *valsobj,bits256 hash,uint32_t timestamp)
{
    char *dest,*src; uint32_t i;
    memset(rp,0,sizeof(*rp));
    if ( (dest= jstr(valsobj,"dest")) != 0 && (src= jstr(valsobj,"source")) != 0 && (rp->srcamount= j64bits(valsobj,"satoshis")) != 0 )
    {
        if ( (rp->destamount= j64bits(valsobj,"destsatoshis")) != 0 )
        {
            rp->desthash = jbits256(valsobj,"desthash");
            for (i=0; i<4; i++)
                if ( rp->desthash.ulongs[i] != 0 )
                    break;
            if ( i != 4 )
                rp->destamount = 0;
        }
        rp->minamount = j64bits(valsobj,"minamount");
        rp->timestamp = timestamp;
        rp->hash = hash;
        strncpy(rp->src,src,sizeof(rp->src)-1);
        strncpy(rp->dest,dest,sizeof(rp->dest)-1);
        rp->requestid = basilisk_requestid(rp);
        if ( rp->destamount != 0 && bits256_nonz(rp->desthash) != 0 )
        {
            rp->quoteid = basilisk_quoteid(rp);
            printf("set quoteid.%u\n",rp->quoteid);
        }
        //printf("create.%u calc.%u\n",rp->requestid,basilisk_requestid(rp));
        return(0);
    }
    return(-1);
}

char *basilisk_start(struct supernet_info *myinfo,struct basilisk_request *rp,uint32_t statebits)
{
    cJSON *retjson;
    if ( (bits256_cmp(rp->hash,myinfo->myaddr.persistent) == 0 || bits256_cmp(rp->desthash,myinfo->myaddr.persistent) == 0) )
    {
        printf("START thread to complete %u/%u for (%s %.8f) <-> (%s %.8f) q.%u\n",rp->requestid,rp->quoteid,rp->src,dstr(rp->srcamount),rp->dest,dstr(rp->destamount),rp->quoteid);
        if ( basilisk_thread_start(myinfo,rp) != 0 )
        {
            basilisk_request_enqueue(myinfo,rp);
            return(clonestr("{\"result\":\"started atomic swap thread\"}"));
        }
        else return(clonestr("{\"error\":\"couldnt atomic swap thread\"}"));
    }
    else if ( myinfo->RELAYID >= 0 )
    {
        retjson = cJSON_CreateObject();
        jaddstr(retjson,"result","basilisk node needs to start atomic thread locally");
        return(jprint(retjson,1));
    } else return(clonestr("{\"error\":\"unexpected basilisk_start not mine and amrelay\"}"));
}

struct basilisk_relay *basilisk_request_ensure(struct supernet_info *myinfo,uint32_t senderipbits,int32_t numrequests)
{
    int32_t j; struct basilisk_relay *relay = 0;
    if ( (j= basilisk_relayid(myinfo,senderipbits)) >= 0 )
    {
        relay = &myinfo->relays[j];
        if ( numrequests > relay->maxrequests )
        {
            relay->maxrequests = numrequests;
            relay->requests = realloc(relay->requests,sizeof(*relay->requests) * numrequests);
        }
    }
    return(relay);
}

int32_t basilisk_ping_processDEX(struct supernet_info *myinfo,uint32_t senderipbits,uint8_t *data,int32_t datalen)
{
    int32_t i,n,len=0; struct basilisk_relay *relay; struct basilisk_request R; uint8_t clen,serialized[256]; uint16_t sn; uint32_t crc;
    portable_mutex_lock(&myinfo->DEX_reqmutex);
    len += iguana_rwnum(0,&data[len],sizeof(sn),&sn);
    if ( (relay= basilisk_request_ensure(myinfo,senderipbits,sn)) != 0 )
    {
        relay->numrequests = 0;
        for (i=0; i<sn; i++)
        {
            clen = data[len++];
            if ( len+clen <= datalen )
            {
                if ( relay->numrequests < relay->maxrequests )
                {
                    memcpy(serialized,&data[len],clen);
                    //printf("ping processDEX\n");
                    n = basilisk_rwDEXquote(0,serialized,&R);
                    if ( n != clen )
                        printf("n.%d clen.%d\n",n,clen);
                    len += clen;
                    crc = basilisk_requestid(&R);
                    if ( crc == R.requestid )
                    {
                        relay->requests[relay->numrequests++] = R;
                        //printf("[(%s %.8f) -> (%s %.8f) r.%u q.%u] ",R.src,dstr(R.srcamount),R.dest,dstr(R.destamount),R.requestid,R.quoteid);
                    } else printf("crc.%u error vs %u\n",crc,R.requestid);
                } else printf("relay num.%d >= max.%d\n",relay->numrequests,relay->maxrequests);
            } else len += clen;
        }
    }
    else
    {
        for (i=0; i<sn; i++)
        {
            if ( len+clen <= datalen )
            {
                clen = data[len++];
                len += clen;
            }
        }
    }
    portable_mutex_unlock(&myinfo->DEX_reqmutex);
    return(len);
}

int32_t basilisk_ping_genDEX(struct supernet_info *myinfo,uint8_t *data,int32_t maxlen)
{
    struct queueitem *item,*tmp; uint8_t clen; int32_t i,datalen = 0; uint16_t sn; uint32_t timestamp,now;
    datalen += sizeof(uint16_t);
    i = 0;
    now = (uint32_t)time(NULL);
    portable_mutex_lock(&myinfo->DEX_mutex);
    DL_FOREACH_SAFE(myinfo->DEX_quotes,item,tmp)
    {
        memcpy(&clen,&item[1],sizeof(clen));
        if ( datalen+clen < maxlen )
        {
            memcpy(&data[datalen],&item[1],clen+1), datalen += (clen + 1);
            i++;
        }
        iguana_rwnum(0,(void *)((long)&item[1] + 1 + sizeof(uint32_t)),sizeof(timestamp),&timestamp);
        if ( now > timestamp + BASILISK_DEXDURATION )
        {
            DL_DELETE(myinfo->DEX_quotes,item);
            free(item);
        } //else printf("now.%u vs timestamp.%u, lag.%d\n",now,timestamp,now-timestamp);
    }
    portable_mutex_unlock(&myinfo->DEX_mutex);
    sn = i;
    iguana_rwnum(1,data,sizeof(sn),&sn); // fill in at beginning
    return(datalen);
}

static int _cmp_requests(const void *a,const void *b)
{
#define uint32_a (*(struct basilisk_request *)a).requestid
#define uint32_b (*(struct basilisk_request *)b).requestid
	if ( uint32_b > uint32_a )
		return(1);
	else if ( uint32_b < uint32_a )
		return(-1);
    else
    {
#undef uint32_a
#undef uint32_b
#define uint32_a (*(struct basilisk_request *)a).quoteid
#define uint32_b (*(struct basilisk_request *)b).quoteid
        if ( uint32_b > uint32_a )
            return(1);
        else if ( uint32_b < uint32_a )
            return(-1);
    }
	return(0);
#undef uint32_a
#undef uint32_b
}

struct basilisk_request *_basilisk_requests_uniq(struct supernet_info *myinfo,int32_t *nump,uint8_t *space,int32_t spacesize)
{
    int32_t i,j,n,k,m; struct basilisk_relay *relay; struct basilisk_request *requests,*rp;
    for (j=m=0; j<myinfo->numrelays; j++)
        m += myinfo->relays[j].numrequests;
    if ( m*sizeof(*requests) <= spacesize )
        requests = (void *)space;
    else requests = calloc(m,sizeof(*requests));
    for (j=m=0; j<myinfo->numrelays; j++)
    {
        relay = &myinfo->relays[j];
        if ( (n= relay->numrequests) > 0 )
        {
            for (i=0; i<n; i++)
            {
                rp = &relay->requests[i];
                for (k=0; k<m; k++)
                    if ( memcmp(&requests[k],rp,sizeof(requests[k])) == 0 )
                        break;
                if ( k == m )
                {
                    requests[m].relaybits = relay->ipbits;
                    requests[m++] = *rp;
                }
            }
        }
    }
    qsort(requests,m,sizeof(*requests),_cmp_requests);
    *nump = m;
    return(requests);
}

char *basilisk_respond_swapstatus(struct supernet_info *myinfo,bits256 hash,uint32_t requestid,uint32_t quoteid)
{
    cJSON *array,*retjson;
    array = cJSON_CreateArray();
    retjson = cJSON_CreateObject();
    jadd(retjson,"result",array);
    return(jprint(retjson,1));
}

char *basilisk_respond_requests(struct supernet_info *myinfo,bits256 hash,uint32_t requestid,uint32_t quoteid)
{
    int32_t i,qflag,num=0; cJSON *retjson,*array; struct basilisk_request *requests,*rp; uint8_t space[4096];
    array = cJSON_CreateArray();
    portable_mutex_lock(&myinfo->DEX_reqmutex);
    if ( (requests= _basilisk_requests_uniq(myinfo,&num,space,sizeof(space))) != 0 )
    {
        //printf("numrequests.%d r.%u q.%u\n",num,requestid,quoteid);
        for (i=0; i<num; i++)
        {
            rp = &requests[i];
            if ( quoteid == 0 || (quoteid == rp->quoteid && (bits256_cmp(hash,rp->hash) == 0 || bits256_cmp(hash,rp->desthash) == 0)) )
                qflag = 1;
            else qflag = 0;
            if ( requestid == 0 || (rp->requestid == requestid && qflag != 0) )
                jaddi(array,basilisk_requestjson(rp));
        }
    }
    portable_mutex_unlock(&myinfo->DEX_reqmutex);
    if ( requests != (void *)space )
        free(requests);
    retjson = cJSON_CreateObject();
    jadd(retjson,"result",array);
    return(jprint(retjson,1));
}

char *basilisk_respond_accept(struct supernet_info *myinfo,uint32_t requestid,uint32_t quoteid)
{
    int32_t i,num=0; char *retstr=0; struct basilisk_request *requests,*rp; uint8_t space[4096];
    portable_mutex_lock(&myinfo->DEX_reqmutex);
    if ( (requests= _basilisk_requests_uniq(myinfo,&num,space,sizeof(space))) != 0 )
    {
        for (i=0; i<num; i++)
        {
            rp = &requests[i];
            if ( rp->requestid == requestid && rp->quoteid == quoteid )
            {
                printf("start from accept\n");
                retstr = basilisk_start(myinfo,rp,1);
                break;
            }
        }
    }
    portable_mutex_unlock(&myinfo->DEX_reqmutex);
    if ( requests != (void *)space )
        free(requests);
    if ( retstr == 0 )
        retstr = clonestr("{\"error\":\"couldnt find to requestid to choose\"}");
    return(retstr);
}

// respond to incoming RID, ACC, DEX, QST

char *basilisk_respond_RID(struct supernet_info *myinfo,char *CMD,void *addr,char *remoteaddr,uint32_t basilisktag,cJSON *valsobj,uint8_t *data,int32_t datalen,bits256 hash,int32_t from_basilisk)
{
    return(basilisk_respond_requests(myinfo,hash,juint(valsobj,"requestid"),0));
}

char *basilisk_respond_SWP(struct supernet_info *myinfo,char *CMD,void *addr,char *remoteaddr,uint32_t basilisktag,cJSON *valsobj,uint8_t *data,int32_t datalen,bits256 hash,int32_t from_basilisk)
{
    return(basilisk_respond_swapstatus(myinfo,hash,juint(valsobj,"requestid"),juint(valsobj,"quoteid")));
}

char *basilisk_respond_ACC(struct supernet_info *myinfo,char *CMD,void *addr,char *remoteaddr,uint32_t basilisktag,cJSON *valsobj,uint8_t *data,int32_t datalen,bits256 hash,int32_t from_basilisk)
{
    uint32_t requestid,quoteid;
    if ( (requestid= juint(valsobj,"requestid")) != 0 && (quoteid= juint(valsobj,"quoteid")) != 0 )
        return(basilisk_respond_accept(myinfo,requestid,quoteid));
    else return(clonestr("{\"error\":\"need nonzero requestid and quoteid\"}"));
}

char *basilisk_respond_DEX(struct supernet_info *myinfo,char *CMD,void *addr,char *remoteaddr,uint32_t basilisktag,cJSON *valsobj,uint8_t *data,int32_t datalen,bits256 hash,int32_t from_basilisk)
{
    char *retstr=0,buf[256]; struct basilisk_request R;
    if ( basilisk_request_create(&R,valsobj,hash,juint(valsobj,"timestamp")) == 0 )
    {
        char str[65]; printf("DEX.(%s %.8f) -> %s %s\n",R.src,dstr(R.srcamount),R.dest,bits256_str(str,hash));
        if ( basilisk_request_enqueue(myinfo,&R) != 0 )
        {
            sprintf(buf,"{\"result\":\"DEX request added\",\"requestid\":%u}",R.requestid);
            retstr = clonestr(buf);
        } else retstr = clonestr("{\"error\":\"DEX quote couldnt be created\"}");
    } else retstr = clonestr("{\"error\":\"missing or invalid fields\"}");
    return(retstr);
}

#include "../includes/iguana_apidefs.h"
#include "../includes/iguana_apideclares.h"

THREE_STRINGS_AND_DOUBLE(tradebot,aveprice,comment,base,rel,basevolume)
{
    double retvals[4],aveprice; cJSON *retjson = cJSON_CreateObject();
    aveprice = instantdex_avehbla(myinfo,retvals,base,rel,basevolume);
    jaddstr(retjson,"result","success");
    jaddnum(retjson,"aveprice",aveprice);
    jaddnum(retjson,"avebid",retvals[0]);
    jaddnum(retjson,"bidvol",retvals[1]);
    jaddnum(retjson,"aveask",retvals[2]);
    jaddnum(retjson,"askvol",retvals[3]);
    return(jprint(retjson,1));
}

ZERO_ARGS(InstantDEX,allcoins)
{
    struct iguana_info *tmp; cJSON *basilisk,*virtual,*full,*retjson = cJSON_CreateObject();
    full = cJSON_CreateArray();
    basilisk = cJSON_CreateArray();
    virtual = cJSON_CreateArray();
    HASH_ITER(hh,myinfo->allcoins,coin,tmp)
    {
        if ( coin->virtualchain != 0 )
            jaddistr(virtual,coin->symbol);
        if ( coin->RELAYNODE != 0 || coin->VALIDATENODE != 0 )
            jaddistr(full,coin->symbol);
        else jaddistr(basilisk,coin->symbol);
    }
    jadd(retjson,"basilisk",basilisk);
    jadd(retjson,"full",full);
    jadd(retjson,"virtual",virtual);
    return(jprint(retjson,1));
}

STRING_ARG(InstantDEX,available,source)
{
    if ( source != 0 && source[0] != 0 && (coin= iguana_coinfind(source)) != 0 )
    {
        if ( myinfo->expiration != 0 )
            return(bitcoinrpc_getbalance(myinfo,coin,json,remoteaddr,"*",coin->chain->minconfirms,1,1<<30));
        else return(clonestr("{\"error\":\"need to unlock wallet\"}"));
    } else return(clonestr("{\"error\":\"specified coin is not active\"}"));
}

HASH_ARRAY_STRING(InstantDEX,request,hash,vals,hexstr)
{
    myinfo->DEXactive = (uint32_t)time(NULL) + INSTANTDEX_LOCKTIME;
    jadd64bits(vals,"minamount",jdouble(vals,"minprice") * jdouble(vals,"amount") * SATOSHIDEN);
    if ( jobj(vals,"desthash") == 0 )
        jaddbits256(vals,"desthash",hash);
    jadd64bits(vals,"satoshis",jdouble(vals,"amount") * SATOSHIDEN);
    jadd64bits(vals,"destsatoshis",jdouble(vals,"destamount") * SATOSHIDEN);
    jaddnum(vals,"timestamp",time(NULL));
    hash = myinfo->myaddr.persistent;
    printf("service.(%s)\n",jprint(vals,0));
    {
        uint8_t serialized[512]; struct basilisk_request R; cJSON *reqjson;
        memset(&R,0,sizeof(R));
        if ( basilisk_request_create(&R,vals,hash,juint(vals,"timestamp")) == 0 )
        {
            printf("R.requestid.%u vs calc %u, q.%u\n",R.requestid,basilisk_requestid(&R),R.quoteid);
            if ( myinfo->RELAYID >= 0 )
                R.relaybits = myinfo->myaddr.myipbits;
            if ( (reqjson= basilisk_requestjson(&R)) != 0 )
                free_json(reqjson);
            printf("R.requestid.%u vs calc %u, q.%u\n",R.requestid,basilisk_requestid(&R),R.quoteid);
            basilisk_rwDEXquote(1,serialized,&R);
            basilisk_rwDEXquote(0,serialized,&R);
        } else printf("error creating request\n");
    }
    return(basilisk_standardservice("DEX",myinfo,0,myinfo->myaddr.persistent,vals,"",1));
}

INT_ARG(InstantDEX,automatched,requestid)
{
    // return quoteid
    myinfo->DEXactive = (uint32_t)time(NULL) + INSTANTDEX_LOCKTIME;
    return(clonestr("{\"result\":\"automatched not yet\"}"));
}

INT_ARG(InstantDEX,incoming,requestid)
{
    cJSON *vals; char *retstr;
    myinfo->DEXactive = (uint32_t)time(NULL) + INSTANTDEX_LOCKTIME;
    if ( myinfo->RELAYID >= 0 )
        return(basilisk_respond_requests(myinfo,myinfo->myaddr.persistent,requestid,0));
    else
    {
        vals = cJSON_CreateObject();
        jaddnum(vals,"requestid",(uint32_t)requestid);
        jaddbits256(vals,"hash",myinfo->myaddr.persistent);
        retstr = basilisk_standardservice("RID",myinfo,0,myinfo->myaddr.persistent,vals,"",1);
        free_json(vals);
        return(retstr);
    }
}

TWO_INTS(InstantDEX,swapstatus,requestid,quoteid)
{
    cJSON *vals; char *retstr;
    myinfo->DEXactive = (uint32_t)time(NULL) + INSTANTDEX_LOCKTIME;
    if ( myinfo->RELAYID >= 0 )
        return(basilisk_respond_swapstatus(myinfo,myinfo->myaddr.persistent,requestid,quoteid));
    else
    {
        vals = cJSON_CreateObject();
        jaddnum(vals,"requestid",(uint32_t)requestid);
        jaddnum(vals,"quoteid",(uint32_t)quoteid);
        jaddbits256(vals,"hash",myinfo->myaddr.persistent);
        retstr = basilisk_standardservice("SWP",myinfo,0,myinfo->myaddr.persistent,vals,"",1);
        free_json(vals);
        return(retstr);
    }
}

TWO_INTS(InstantDEX,accept,requestid,quoteid)
{
    cJSON *vals; char *retstr;
    myinfo->DEXactive = (uint32_t)time(NULL) + INSTANTDEX_LOCKTIME;
    if ( myinfo->RELAYID >= 0 )
        return(basilisk_respond_accept(myinfo,requestid,quoteid));
    else
    {
        vals = cJSON_CreateObject();
        jaddnum(vals,"quoteid",(uint32_t)quoteid);
        jaddnum(vals,"requestid",(uint32_t)requestid);
        retstr = basilisk_standardservice("ACC",myinfo,0,myinfo->myaddr.persistent,vals,"",1);
        free_json(vals);
        return(retstr);
    }
}
#include "../includes/iguana_apiundefs.h"
