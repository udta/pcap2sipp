/*
 * pcap2sipp     - tool for generating the sipp scenario, injection file and RTP packets 
 * that are needed to run a sipp test that simulates the behavior from a given pcap trace
 * Copyright (c) 2012 Catalina Oancea
 *
 * * * BEGIN LICENCE * * *
 *
 * This file is part of pcap2sipp
 *
 * pcap2sipp is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * pcap2sipp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with pcap2sipp.  If not, see <http://www.gnu.org/licenses/>.
 *
 * * * END LICENCE * * *
 *
 */

#include "pcap2sipp.h"
#include "nodes.h"
#include "strings.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <libnet.h>
#include <libnet/libnet-headers.h>

int to_string( ip_address ip, char *result )
{
    sprintf( result, "%d.%d.%d.%d", ip.byte1, ip.byte2, ip.byte3, ip.byte4 );
    return 0;
}

int is_sip( char *sc )
{
    char *token;
    if ( strncasecmp( sc, SIP_IDENTIFYER, 3 ) == 0 )
        return 1;
    token = strchr( sc, ' ' );
    if ( token )
    {
        if ( strncmp( token + 1, SIP_IDENTIFYER, 3 ) == 0 )
            return 1;
        else
            return 0;
    }
    return 0;
}

int what_to_wait_for( char **text, int *type, char *sc )
{
    char *pos;
    if ( strncmp( sc, "SIP/2.0", 7 ) == 0 )
    {
        *type = REPLY;
        *text = malloc( sizeof(char) * 4 );
        if ( (*text) == NULL )
        {
            fprintf( stderr, "Cannot allocate memory\n" );
            return -1;
        }
        (*text)[0] = sc[8];
        (*text)[1] = sc[9];
        (*text)[2] = sc[10];
        (*text)[3] = '\0';
    }
    else
    {
        *type = REQUEST;
        pos = strchr( sc, ' ' );
        *text = malloc( sizeof(char) * ((pos - sc) + 1) );
        if ( (*text) == NULL )
        {
            fprintf( stderr, "Cannot allocate memory\n" );
            return -1;
        }
        strncpy( *text, sc, (pos - sc) );
        (*text)[pos - sc] = '\0';
    }
    return 0;
}

char* get_callid( char *sc )
{
    char *pch=NULL, *str=NULL;

    str = malloc( sizeof(char) * strlen( sc ) );
    if ( str == NULL )
    {
        fprintf( stderr, "Cannot allocate memory\n" );
        return NULL;
    }

    strncpy( str, sc, strlen( sc ) );
    pch = strtok( str, "\r\n" );
    //printf( "GETCID >>> %s\n", pch );

    while ( pch != NULL )
    {
        //printf( "GET--CID >>> %s\n", pch );
        if ( strncmp( pch, "Call-ID:", strlen( "Call-ID:" ) ) == 0 )
        {
            char *tmp = strstr( pch, ":" )+1;
            char *tmp2 = strstr( pch, ": " )+2;

            return tmp2 > tmp ? tmp2 : tmp;
        }
        else
            pch = strtok( NULL, "\r\n" );

    }
    return NULL;
}

char* get_sip_method( char *sc, int type )
{
    char *pch=NULL;
    char method[64] = {0};

    pch = strcasestr( sc, "SIP/2.0" );
    if ( pch != NULL )
    {
        if ( type == REPLY )
        {
            /*SIP/2.0 100 Trying*/
            sscanf( pch+8, "%*d %s", method);
            if ( method[0] != '0' )
            {
                printf( "REPLY Method is [%s]\n", method );
                return strdup( method );
            }
        }
        else
        {
            /*INVITE sip:9725552007@[remote_ip]:[remote_port] SIP/2.0*/
            char buffer[512] = {0};
            strncpy( buffer, sc, sc-pch>sizeof( buffer ) ? sizeof( buffer )-1 : sc-pch); 
            sscanf( buffer, "%s %*s ", method);
            {
                printf( "REQUEST Method is [%s]\n", method );
                return strdup( method );
            }
        }

    }

    return NULL;
}

int has_totag( char *sc )
{
    char *pch, *str;

    str = malloc( sizeof(char) * strlen( sc ) );
    if ( str == NULL )
    {
        fprintf( stderr, "Cannot allocate memory\n" );
        return -1;
    }

    strncpy( str, sc, strlen( sc ) );
    pch = strtok( str, "\n" );

    while ( pch != NULL )
    {
        if ( strlen( pch ) > 3 )
        {
            if ( strncmp( pch, "To:", 3 ) == 0 )
            {
                char *match = strstr( pch, "tag=" );
                if ( match != NULL )
                    return 1;
                else
                    return 0;
            }
            else
                pch = strtok( NULL, "\n" );
        }
        else
            pch = strtok( NULL, "\n" );
    }

    return 0;
}

int header_has_tag( char *sc )
{
    char *match = strstr( sc, "tag=" );

    if ( match != NULL )
        return 1;
    else
        return 0;
}

int ethernet_length( const u_char *pkt_data, const int datalink )
{
    ethernet_header *eth_hdr;

    if ( pkt_data == NULL )
    {
        return -1;
    }


    switch ( datalink )
    {
        case DLT_EN10MB:
            eth_hdr = (ethernet_header *)pkt_data;

            switch ( ntohs( eth_hdr->ether_type ) )
            {
                case ETHERTYPE_VLAN:
                    return 18;
                    break;

                case ETHERTYPE_IP:
                    return 14;
                    break;
                default:
                    //fprintf(stderr, "ERROR: Unsupported datalink type %d \n",pcap_datalink_val_to_name(datalink));
                    return -1;
            }
            break;

        case DLT_LINUX_SLL:
            return 16;
            //fprintf(stderr, "linx ssl %s \n",pcap_datalink_val_to_name(datalink));
            break;
        default:
            //fprintf(stderr, "ERROR: Unsupported datalink type %s \n",pcap_datalink_val_to_name(datalink));
            return -1;
    }
    return -1;
}


void packet_handler_ips( u_char *param, const struct pcap_pkthdr *header, const u_char *pkt_data )
{
    ip_header *ih = NULL;
    udp_header *uh;
    char *sc;
    u_int ip_len;
    u_short sport, dport;

    /* retrieve the position of the ip header */
    int datalink_length = ethernet_length( pkt_data, datalink );
    if ( datalink_length == -1 )
        return;

    ih = (ip_header *)(pkt_data + datalink_length);

    /* retrieve the position of the udp header */
    ip_len = (ih->ver_ihl & 0xf) * 4;

    uh = (udp_header *)((u_char *)ih + ip_len);

    /* retrieve the SIP contents */
    sc = (char *)((u_char *)uh + (sizeof(u_short)) * 4);
    sc[header->len - ip_len - (sizeof(u_short)) * 4 - 16] = '\0';

    sport = ntohs( uh->sport );
    dport = ntohs( uh->dport );

    if ( (sport == 5060) || (dport == 5060) || (is_sip( sc )) )
    {
        if ( !in_ipnodelist( ip_addresses, ih->saddr, sport ) )
        {
            add_ipnodes( &ip_addresses, ih->saddr, sport );
        }
        if ( !in_ipnodelist( ip_addresses, ih->daddr, dport ) )
        {
            add_ipnodes( &ip_addresses, ih->daddr, dport );
        }
    }
}

char* handle_requestline( char *rep, char *daddr )
{
    char *newto;
    char *nothing;

    if ( remote_nr != NULL )
    {
        newto = malloc( sizeof(char) * (strlen( "[field2]" ) + 4) );
        if ( newto == NULL )
        {
            fprintf( stderr, "Cannot allocate memory\n" );
            return NULL;
        }
        sprintf( newto, "sip:%s", "[field2]" );
        rep = replace_after( rep, "sip:", "@", newto, 1 );
        if ( rep == NULL )
            return NULL;

    }

    if ( remote_port != 5060 )
        rep = replace( rep, daddr, "[remote_ip]:[remote_port]", IP_STRING );
    else
        rep = replace( rep, daddr, "[remote_ip]", IP_STRING );

    //snom Phone often add the line parameter to identify the line. we need to remove it because it will make calls fail
    nothing = malloc( sizeof(char) );
    sprintf( nothing, " " );
    rep = replace_after( rep, ";line=", " ", nothing, 1 );
    if ( rep == NULL )
        return NULL;

    return rep;
}

int handle_receive( char *text, int type )
{
    if ( type == REPLY )
    {

        //if a reply gets repeated, it might be that is is just a retransmission. We need to make it optional so it doesn't mess up the test
        char *optional_tag;
        /*if ( (strncmp(text,last_reply,3) == 0) )
                optional_tag="optional=\"true\"";
        else */
        optional_tag = "";

        if ( strncmp( text, last_reply, 3 ) != 0 )
        {
            if ( (strcmp( text, "401" ) != 0) || (strcmp( text, "407" ) != 0) )
                fprintf( scenario_file, "<recv response=\"%s\" auth=\"true\" %s></recv>\n", text, optional_tag );
            else
                fprintf( scenario_file, "<recv response=\"%s\" %s></recv>\n", text, optional_tag );
        }

        //set the last reply so we know for the next one to avoid adding unneeded retransmissions
        strncpy( last_reply, text, 3 );
    }
    else
    {
        if ( (strcmp( text, "ACK" ) != 0) && (strcmp( text, "PRACK" ) != 0) )
        {
            fprintf( scenario_file, "<recv request=\"%s\"><action>\n"
                     "  <ereg regexp=\"branch=([[:alnum:]-]*)\" search_in=\"hdr\" header=\"Via:\" assign_to=\"1,2\" />\n"
                     "  <ereg regexp=\"tag=([[:alnum:]-]*)\" search_in=\"hdr\" header=\"From:\" assign_to=\"3,4\" />\n"
                     "  <ereg regexp=\".*\" search_in=\"hdr\" header=\"CSeq:\" assign_to=\"5\" />\n"
                     "</action></recv>\n", text );
            fprintf( scenario_file, "\n" );
            fprintf( scenario_file, "<nop><action>\n"
                     "  <assign assign_to=\"1\" value=\"0\" />\n"
                     "  <assign assign_to=\"3\" value=\"0\" />\n"
                     "</action></nop>\n" );
        }
        else
        {
            fprintf( scenario_file, "<recv request=\"%s\"><action>\n"
                     "  <ereg regexp=\"branch=([[:alnum:]-]*)\" search_in=\"hdr\" header=\"Via:\" assign_to=\"1,2\" />\n"
                     "  <ereg regexp=\"tag=([[:alnum:]-]*)\" search_in=\"hdr\" header=\"From:\" assign_to=\"3,4\" />\n"
                     /*"  <ereg regexp=\".*\" search_in=\"hdr\" header=\"From:\" assign_to=\"5\" />\n"
                     "  <ereg regexp=\".*\" search_in=\"hdr\" header=\"To:\" assign_to=\"6\" />\n"
                     "  <ereg regexp=\"<(.*)>\" search_in=\"hdr\" header=\"Contact:\" assign_to=\"7,8\" />\n"*/
                     "</action></recv>\n", text );
            fprintf( scenario_file, "\n" );
            fprintf( scenario_file, "<nop><action>\n"
                     "  <assign assign_to=\"1\" value=\"0\" />\n"
                     "  <assign assign_to=\"3\" value=\"0\" />\n"
                     /*"  <assign assign_to=\"7\" value=\"0\" />\n"*/
                     "</action></nop>\n" );
        }
    }
    fprintf( scenario_file, "\n" );

    return 0;
}

char* handle_to_header( char *rep, int type, char *saddr, char *daddr )
{
    char *newto = NULL;

    //printf( "TO ::[%s]\n", rep );

    if ( type == REPLY )
    {
        return strdup( "[last_To:]" );
    }
    else
    {
        if ( remote_name != NULL )
        {
            newto = realloc( newto, sizeof(char) * (strlen( "[field3]" ) + 4) );
            if ( newto == NULL )
            {
                fprintf( stderr, "Cannot reallocate memory\n" );
                return NULL;
            }

            if ( strstr( rep,  "To: \"") != NULL )
            {
                sprintf( newto, "To: \"%s", "[field3]" );
                rep = replace_after( rep, "To: \"", "\"", newto, 1 );
            }
            else if (  strstr( rep,  "To:\"") != NULL )
            {
                sprintf( newto, "To:\"%s", "[field3]" );
                rep = replace_after( rep, "To:\"", "\"", newto, 1 );
            }
            if ( rep == NULL )
                return NULL;
        }

        if ( remote_nr != NULL )
        {
            newto = malloc( sizeof(char) * (strlen( "[field2]" ) + 4) );
            if ( newto == NULL )
            {
                fprintf( stderr, "Cannot allocate memory\n" );
                return NULL;
            }
            sprintf( newto, "sip:%s", "[field2]" );

            rep = replace_after( rep, "sip:", "@", newto, 1 );
            if ( rep == NULL )
                return NULL;
        }

#if 0
        if ( remote_port != 5060 )
            rep = replace( rep, daddr, "[remote_ip]:[remote_port]", IP_STRING );
        else
            rep = replace( rep, daddr, "[remote_ip]", IP_STRING );
        if ( rep == NULL )
            return NULL;
#else
        rep = replace_after( rep, "@", ">", "@[remote_ip]:[remote_port]", 1 );
#endif
        if ( header_has_tag( rep ) )
        {
            rep = replace_after( rep, ";tag=", ";", ";tag=[$4]", 1 );
            if ( rep == NULL )
                return NULL;
        }
        
    }

    //printf( "\n\nTO DONE::[%s]\n", rep );

    return rep;
}

char* handle_from_header( char *rep, int type, char *saddr, char *daddr )
{
    char *newfrom;
    size_t size;
    char *newstring;
    char *newtag;

    if ( type == REPLY )
    {
        size = sizeof(char) * (1 + strlen( "[last_From:]\r\n" ));

        newstring = malloc( size );
        if ( newstring == NULL )
        {
            fprintf( stderr, "Cannot allocate memory\n" );
            return NULL;
        }

        strcpy( newstring, "[last_From:]" );
        rep = newstring;
    }
    else
    {
        newtag = malloc( sizeof(char) * (strlen( call_number ) + 4) );
        if ( newtag == NULL )
        {
            fprintf( stderr, "Cannot allocate memory\n" );
            return NULL;
        }

        sprintf( newtag, "tag=%s", call_number );

        rep = replace_after( rep, "tag=", ";", newtag, 1 );
        if ( rep == NULL )
            return NULL;

        if ( local_port != 5060 )
            rep = replace( rep, saddr, "[local_ip]:[local_port]", IP_STRING );
        else
            rep = replace( rep, saddr, "[local_ip]", IP_STRING );
        if ( rep == NULL )
            return NULL;

        if ( local_nr != NULL )
        {
            newfrom = malloc( sizeof(char) * (strlen( "[field0]" ) + 4) );
            if ( newfrom == NULL )
            {
                fprintf( stderr, "Cannot allocate memory\n" );
                return NULL;
            }

            sprintf( newfrom, "sip:%s", "[field0]" );

            rep = replace_after( rep, "sip:", "@", newfrom, 1 );
            if ( rep == NULL )
                return NULL;
        }
    }

    return rep;
}

char* handle_contact_header( char *rep, char *saddr )
{
    //printf( "CONTACT START:: [%s] (%s)\n", rep, saddr );
    char *newto;

    if ( local_nr != NULL )
    {
        /*Process ACCOUNT NAME ON URI*/
        newto = malloc( sizeof(char) * (strlen( "[field0]" ) + 5) );
        memset( newto, 0, sizeof(char) * (strlen( "[field0]" ) + 5) );

        if ( newto == NULL )
        {
            fprintf( stderr, "Cannot allocate memory\n" );
            return NULL;
        }

        if ( strchr( rep, '@' ) != NULL )
        {
            sprintf( newto, "sip:%s", "[field0]" );
            rep = replace_after( rep, "sip:", "@", newto, 1 );
        }
        else
        {
            sprintf( newto, "sip:%s@", "[field0]" );
            rep = replace( rep, "sip:", newto, ANY_STRING );
        }
        if ( rep == NULL )
            return NULL;
    }

    if ( strstr( rep, saddr ) != NULL )
    {
        //printf( "URI included src IP addr\n" );
        if ( local_port != 5060 )
            rep = replace( rep, saddr, "[local_ip]:[local_port];transport=[transport]", IP_STRING );
        else
            rep = replace( rep, saddr, "[local_ip];transport=[transport]", IP_STRING );
    }
    else
    {
        //printf( "URI have no src IP addr, maybe a domain!!\n" );
        if ( strchr( rep, '@' ) != NULL )
        {
            rep = replace_after( rep, "@", ">", "@[local_ip]:[local_port];transport=[transport]", 1 );
        }
        else
        {
            rep = replace_after( rep, "sip:", ">", "sip:[local_ip]:[local_port];transport=[transport]", 1 );
        }
        //if ( local_port != 5060 )
        //    rep = replace( rep, saddr, "[local_ip]:[local_port]", IP_STRING );
        //else
        //    rep = replace( rep, saddr, "[local_ip]", IP_STRING );
    }

    //printf( "CONTACT DONE:: [%s]\n", rep );
    return rep;
}

char* handle_callid_header( char *rep )
{
    size_t size;
    char *newstring;

    if ( i_am_initiator == 1 )
    {
        rep = replace_after( rep, "Call-ID: ", ";", "Call-ID: [call_id]", 1 );
        if ( rep == NULL )
            return NULL;
    }
    else
    {
        size = sizeof(char) * (1 + strlen( "[last_Call-ID:]\r\n" ));

        newstring = malloc( size );
        if ( newstring == NULL )
        {
            fprintf( stderr, "Cannot allocate memory\n" );
            return NULL;
        }

        strcpy( newstring, "[last_Call-ID:]" );
        rep = newstring;
    }

    return rep;
}

char* handle_cseq_header( char *rep, int type )
{
    size_t size;
    char *newstring;

    if ( type == REPLY )
    {
        size = sizeof(char) * (1 + strlen( "[last_CSeq:]" ));

        newstring = malloc( size );
        if ( newstring == NULL )
        {
            fprintf( stderr, "Cannot allocate memory\n" );
            return NULL;
        }

        strcpy( newstring, "CSeq: [$5]" );
        rep = newstring;
    }
    return rep;
}

char* handle_via_header( char *rep, int type, char *saddr, char *daddr )
{
    if ( type == REPLY )
    {
        #if 1
        rep = strdup( "Via: [last_Via:]" );
        #else
        rep = replace_after( rep, ";branch=", ";", ";branch=[$2]", 1 );
        if ( rep == NULL )
            return NULL;

        rep = replace_after( rep, "received=", ";", "received=[remote_ip]", 1 );
        if ( rep == NULL )
            return NULL;

        if ( remote_port != 5060 )
            rep = replace( rep, saddr, "[remote_ip]:[remote_port]", IP_STRING );
        else
            rep = replace( rep, daddr, "[remote_ip]", IP_STRING );
        if ( rep == NULL )
            return NULL;

        rep = replace_after( rep, "rport=", ";", "rport=[remote_port]", 1 );
        #endif
    }
    else
    {
        rep = replace_after( rep, "received=", ";", "received=[local_ip]", 1 );
        if ( rep == NULL )
            return NULL;

        if ( local_port != 5060 )
            rep = replace( rep, saddr, "[local_ip]:[local_port]", IP_STRING );
        else
            rep = replace( rep, saddr, "[local_ip]", IP_STRING );

        /*Process branch*/
        rep = replace_after( rep, ";branch=", ";", ";branch=[branch]", 1 );
    }

    return rep;
}

char* handle_prauthorization_header( char *rep )
{
    size_t size;
    char *newstring;

    size = sizeof(char) * (1 + strlen( "[field4]" ));

    newstring = malloc( size );
    if ( newstring == NULL )
    {
        fprintf( stderr, "Cannot allocate memory\n" );
        return NULL;
    }

    strcpy( newstring, "[field4]" );

    return newstring;
}


char* handle_authorization_header( char *rep )
{
    size_t size;
    char *newstring;

    size = sizeof(char) * (1 + strlen( "[field4]" ));

    newstring = malloc( size );
    if ( newstring == NULL )
    {
        fprintf( stderr, "Cannot allocate memory\n" );
        return NULL;
    }

    strcpy( newstring, "[field4]" );

    return newstring;
}

char* handle_sdp_c_header( char *rep )
{
    //printf( "SDP C:: [%s]\n", rep );
    
    if ( strcasestr( rep, "c=IN IP" ) != NULL )
    {
        rep = strdup( "c=IN IP[media_ip_type] [media_ip]\r\n" );
    }
    //printf( "SDP C DONE:: [%s]\n", rep );

    return rep;
}

char* handle_sdp_m_header( char *rep )
{
    //printf( "SDP M:: [%s]\n", rep );
    char *start_port = strstr( rep, " " ) + 1;
    char *end_port = strstr( start_port, " " );
    size_t portlen = sizeof(char) * (1 + end_port - start_port);

    char *newstring = malloc( portlen );
    if ( newstring == NULL )
    {
        fprintf( stderr, "Cannot allocate memory\n" );
        return NULL;
    }

    strncpy( newstring, start_port, portlen - 1 );
    newstring[portlen] = '\0';
    sdp_port = atoi( newstring );
    //printf( "SDP port:: [%d]\n", sdp_port );

    rep = replace(rep, newstring, "[media_port]", ANY_STRING);
    //printf( "SDP M DONE:: [%s]\n", rep );

    return rep;
}

char* handle_all_headers( char *rep, int type, char *saddr, char *daddr )
{
    //printf( "ALL HEADER START:: [%s]\n", rep );

    if ( strcasestr(rep, "Content-Length:" ) != NULL )
    {
        //Changed the content-length.
        rep = strdup( "Content-Length:[len]\r\n" );
    }
    else
    {
        rep = replace( rep, saddr, "[local_ip]", IP_STRING );
        if ( rep == NULL )
            return NULL;

        rep = replace( rep, daddr, "[remote_ip]", IP_STRING );
        if ( rep == NULL )
            return NULL;

        rep = replace( rep, "\r", "", ANY_STRING );
        if ( rep == NULL )
            return NULL;
    }

    //printf( "ALL HEADER DONE:: [%s]\n", rep );


    return rep;
}


void packet_handler_simulate( u_char *param, const struct pcap_pkthdr *header, const u_char *pkt_data )
{
    ip_header *ih;
    udp_header *uh;
    char *sc;
    u_int ip_len;
    u_short sport, dport;
    char *saddr = NULL;
    char *daddr = NULL;
    char *text;
    int type;
    char *callid = NULL;
    char *method = NULL;
    char *pch, *str;
    char *rep = NULL;
    int line_nr = 0;
    char *ip_port = NULL;
    char *tmp = NULL;
    char cseq[128] = {0}; 

    /* retrieve the position of the ip header */
    int datalink_length = ethernet_length( pkt_data, datalink );
    if ( datalink_length == -1 )
        return;

    ih = (ip_header *)(pkt_data + datalink_length);

    /* retrieve the position of the udp header */
    ip_len = (ih->ver_ihl & 0xf) * 4;

    uh = (udp_header *)((u_char *)ih + ip_len);

    /* retrieve the SIP contents */
    sc = (char *)((u_char *)uh + (sizeof(u_short)) * 4);
    sc[header->len - ip_len - (sizeof(u_short)) * 4 - 16] = '\0';

    sport = ntohs( uh->sport );
    dport = ntohs( uh->dport );
    //printf("\n\n\n%d %d %d --------------- \n", sport, dport, is_sip( sc ));

    saddr = malloc( 16 * sizeof(char) );
    if ( saddr == NULL )
    {
        fprintf( stderr, "Cannot allocate memory\n" );
        return;
    }
    daddr = malloc( 16 * sizeof(char) );
    if ( daddr == NULL )
    {
        fprintf( stderr, "Cannot allocate memory\n" );
        return;
    }

    to_string( ih->daddr, daddr );
    to_string( ih->saddr, saddr );
    //printf( "Source IP: %s >>> Dest IP %s\n", saddr, daddr );
    //printf( "SIP: %s\n", sc );

    if ( (sport == 5060) || (dport == 5060) || (is_sip( sc )) )
    {
        callid = get_callid( sc );
        if ( callid != NULL )
        {
            //printf( "CALL ID [%s] [%s]\n", callid, sim_callid );
            if ( strstr( sim_callid, callid ) != NULL )
            {
                //printf( "SAME CALL ID\n" );
                what_to_wait_for( &text, &type, sc );
                //printf( "wait for [%s] [%d]\n", text, type );
                method = get_sip_method( sc, type );

                if ( curr_cid[0] == '\0' )
                {
                    strncpy( curr_cid,  callid, sizeof( curr_cid ) - 1);
                }
                else if ( strcmp( callid, curr_cid ) != 0 )
                {
                    printf( "Update CALL ID [%s] >> [%s]\n", curr_cid, callid );
                    strncpy( curr_cid, callid, sizeof( curr_cid ) - 1);
                    this_is_first_request = 1;/*Reset for process next CID*/
                    memset( curr_cseq, 0, sizeof( curr_cseq ) );
                }

                if ( (this_is_first_request == 1) && (type == REPLY) )
                {
                    fprintf( stderr, "ERROR: scenario incomplete, started with a reply" );
                }

                /*Avoiding retransmission SIP packages*/
                tmp = strcasestr( sc, "CSeq:" );
                if ( tmp != NULL )
                {
                    sscanf( tmp+5, "%[^\r\n]", cseq );

                    if ( strcmp( cseq, curr_cseq ) == 0 )
                    {
                        if ( type == REQUEST )
                        {
                            printf( "Avoiding retransmission SIP REQUEST [%s] <%s>\n", curr_cid, cseq );
                            return;
                        }
                        else if ( strcmp(curr_method, method) == 0 )
                        {
                            printf( "Avoiding retransmission SIP Reply [%s] <%s>\n", curr_cid, cseq ); 
                            free( method );
                            return;
                        }
                    }
                    else
                    {
                        strncpy( curr_cseq, cseq, sizeof( curr_cseq ) - 1 );
                    }
                }

                if ( method != NULL )
                {
                    strncpy( curr_method, method, sizeof( curr_method ) - 1 );
                    free( method );
                }

                //printf("%d %d %d  --------------- \n",port, sport, dport);
                //printf("%s %s %s --------------- \n", sim_ip, saddr, daddr);
                //printf( "TEXT: %s\n", text );

                if ( strcmp( sim_ip, saddr ) == 0 && sport == port )
                {
                    //printf("handle receive \n");
                    handle_receive( text, type );
                }

                if ( strcmp( sim_ip, daddr ) == 0 && dport == port )
                {
                    //printf("handle request \n");
                    if ( type == REQUEST && strcasecmp( curr_method, "INVITE" ) == 0 )
                    {
                        //printf("Sending a INVITE!!!!\n");
                        //printf( "package:  [%s]\n", sc );
                        need_remote_number = 1;
                    }

                    //this is a request. reset the last_reply string
                    strcpy( last_reply, "000" );

                    if ( this_is_first_request == 1 )
                        i_am_initiator = 1;
                    fprintf( scenario_file, "<send>\n<![CDATA[\n\n" );

                    str = malloc( sizeof(char) * strlen( sc ) );
                    if ( str == NULL )
                    {
                        fprintf( stderr, "Cannot allocate memory\n" );
                        return;
                    }

                    strncpy( str, sc, strlen( sc ) );

                    pch = strtok( str, "\n" );

                    while ( pch != NULL )
                    {
                        rep = pch;

                        ip_port = malloc( sizeof(char) * (strlen( saddr ) + 7) );
                        if ( ip_port == NULL )
                        {
                            fprintf( stderr, "Cannot allocate memory\n" );
                            return;
                        }

                        sprintf( ip_port, "%s:%d", saddr, sport );

                        rep = replace( rep, ip_port, "[local_ip]:[local_port]", IP_STRING );
                        if ( rep == NULL )
                            return;

                        ip_port = realloc( ip_port, sizeof(char) * (strlen( daddr ) + 7) );
                        if ( ip_port == NULL )
                        {
                            fprintf( stderr, "Cannot reallocate memory\n" );
                            return;
                        }

                        sprintf( ip_port, "%s:%d", daddr, dport );

                        rep = replace( rep, ip_port, "[remote_ip]:[remote_port]", IP_STRING );
                        if ( rep == NULL )
                            return;

                        ip_port = realloc( ip_port, sizeof(char) * (strlen( saddr ) + 7) );
                        if ( ip_port == NULL )
                        {
                            fprintf( stderr, "Cannot reallocate memory\n" );
                            return;
                        }

                        sprintf( ip_port, "%s:", saddr );

                        //preventing the IPs we must replace to appear with any other ports than the ones we've set up
                        rep = replace_after( rep, ip_port, ">", "[local_ip]:[local_port]", 1 );
                        if ( rep == NULL )
                            return;

                        ip_port = realloc( ip_port, sizeof(char) * (strlen( saddr ) + 7) );
                        if ( ip_port == NULL )
                        {
                            fprintf( stderr, "Cannot reallocate memory\n" );
                            return;
                        }

                        sprintf( ip_port, "%s:", daddr );

                        rep = replace_after( rep, ip_port, ">", "[remote_ip]:[remote_port]", 1 );
                        if ( rep == NULL )
                            return;

                        if ( line_nr == 0 )
                        {
                            if ( type == REQUEST )
                            {
                                rep = handle_requestline( rep, daddr );
                                if ( rep == NULL )
                                    return;
                            }
                            line_nr++;
                        }
                        else if ( strncmp( rep, "To:", 3 ) == 0 )
                        {
                            rep = handle_to_header( rep, type, saddr, daddr );
                            if ( rep == NULL )
                                return;
                        }
                        else if ( strncmp( rep, "From:", 5 ) == 0 )
                        {
                            rep = handle_from_header( rep, type, saddr, daddr );
                            if ( rep == NULL )
                                return;
                        }
                        else if ( strncmp( rep, "Contact:", 8 ) == 0 )
                        {
                            rep = handle_contact_header( rep, saddr );
                            if ( rep == NULL )
                                return;
                        }
                        else if ( strncmp( rep, "Call-ID:", 8 ) == 0 )
                        {
                            rep = handle_callid_header( rep );
                            if ( rep == NULL )
                                return;
                        }
                        else if ( strncmp( rep, "CSeq:", 5 ) == 0 )
                        {
                            rep = handle_cseq_header( rep, type );
                            if ( rep == NULL )
                                return;
                        }
                        else if ( strncmp( rep, "Via:", 4 ) == 0 )
                        {
                            rep = handle_via_header( rep, type, saddr, daddr );
                            if ( rep == NULL )
                                return;
                        }
                        else if ( strncmp( rep, "Authorization:", strlen( "Authorization:" ) ) == 0 )
                        {
                            rep = handle_authorization_header( rep );
                            if ( rep == NULL )
                                return;
                        }
                        else if ( strncmp( rep, "Proxy-Authorization:", strlen( "Proxy-Authorization:" ) ) == 0 )
                        {
                            rep = handle_prauthorization_header( rep );
                            if ( rep == NULL )
                                return;
                        }
                        else if ( strncmp( rep, "c=IN ", strlen( "c=IN " ) ) == 0 )
                        {
                            rep = handle_sdp_c_header( rep );
                            if ( rep == NULL )
                                return;
                        }
                        else if ( strncmp( rep, "m=audio", strlen( "m=audio" ) ) == 0 )
                        {
                            rep = handle_sdp_m_header( rep );
                            if ( rep == NULL )
                                return;
                        }
                        else if ( strlen( pch ) < 4 && (strcmp( pch, "\r" ) != 0) )
                        {
                            pch = strtok( NULL, "\n" );
                            continue;
                        }

                        rep = handle_all_headers( rep, type, saddr, daddr );
                        if ( rep == NULL )
                            return;

                        fprintf( scenario_file, "	%s\n", rep );

                        //next line
                        pch = strtok( NULL, "\n" );
                    }
                    fprintf( scenario_file, "\n]]>\n</send>\n" );
                    fprintf( scenario_file, "\n\n" );
                }
                this_is_first_request = 0;
            } //if ( strcmp(sim_callid, callid)==0 )
        }
        else
        {
            printf( "CALL ID is NULL!!!\n" );
        }
    }
    else if ( sport == sdp_port )
    {
        if ( last_sdp == 0 )
        {
            sdp_start_time = *(&header->ts.tv_sec);

            fprintf( scenario_file, "<nop><action><exec play_pcap_audio=\"./rtp.pcap\"/></action></nop>\n" );
            fprintf( scenario_file, "<pause/>\n" );

            last_sdp = 1;
        }
        else
            sdp_end_time = *(&header->ts.tv_sec);
    }
}

void packet_handler_callids( u_char *param, const struct pcap_pkthdr *header, const u_char *pkt_data )
{
    ip_header *ih;
    udp_header *uh;
    char *sc;
    u_int ip_len;
    u_short sport, dport;
    char *callid;

    /* retrieve the position of the ip header */
    int datalink_length = ethernet_length( pkt_data, datalink );
    if ( datalink_length == -1 )
        return;

    ih = (ip_header *)(pkt_data + datalink_length);

    /* retrieve the position of the udp header */
    ip_len = (ih->ver_ihl & 0xf) * 4;

    uh = (udp_header *)((u_char *)ih + ip_len);

    /* retrieve the SIP contents */
    sc = (char *)((u_char *)uh + (sizeof(u_short)) * 4);
    sc[header->len - ip_len - (sizeof(u_short)) * 4 - 16] = '\0';

    sport = ntohs( uh->sport );
    dport = ntohs( uh->dport );

    if ( (sport == 5060) || (dport == 5060) || (is_sip( sc )) )
    {
        callid = get_callid( sc );
        if ( callid != NULL )
            if ( !in_stringlist( callids, callid ) )
                add_stringnodes( &callids, callid );
    }
}


void print_usage()
{
    printf( "pcap2sipp Version 1.0 Beta \n" );
    printf( "Usage: pcap2sipp [options] \n"
            "  Available options:\n"
            "  -o,--option			specify what action to do (listips, listcallids or simulate)\n"
            "  -f,--file			pcap file\n"
            "  -c,--call_id			CallID to simulate (use -o listcallids to get a list of available CallIDs)\n"
            "  -i,--ip			IP which corresponds the DUT in the pcap file (use -o listips to get a list of available IPs)\n"
            "  -p,--port			port which corresponds the DUT in the pcap file\n"
            "  -a,--remote_ip		remote IP - IP of DUT \n"
            "  -d,--remote_port		remote port - port of DUT\n"
            "  -b,--local_ip			local IP\n"
            "  -j,--local_port		local port\n"
            "  -r,--remote_nr		phone number of DUT\n"
            "  -e,--remote_name		caller ID name of DUT\n"
            "  -l,--local_nr			local phone number file\n"
            "  -g,--local_name		local caller ID name file\n"
            "  -s,--local_password		password for local phone number - only needed if DUT sends an authentication request\n" );
}

int read_configs( int argc, char **argv )
{
    char c;
    int option_index = 0;
    int index;
    sim_callid = NULL;
    port = 0;
    ovalue = NULL;
    fvalue = NULL;
    sim_ip = NULL;
    opterr = 0;
    local_ip = NULL;
    remote_ip = NULL;
    local_nr = NULL;
    local_name = NULL;
    remote_nr = NULL;
    remote_name = NULL;
    local_port = 0;
    remote_port = 0;
    local_password = NULL;

    while ( (c = getopt_long( argc, argv, "hd:j:r:e:l:g:a:b:o:c:f:i:p:s:", long_options, &option_index )) != -1 )
        switch ( c )
        {
            case 'h':
                print_usage();
                return ( 1 );
            case 'o':
                if ( strncmp( optarg, "listips", 7 ) != 0 && strncmp( optarg, "listcallids", 12 ) != 0 && strncmp( optarg, "simulate", 8 ) != 0 )
                {
                    fprintf( stderr, "Please specify what action to do (listips, listcallids or simulate)\n" );
                    return -1;
                }
                ovalue = optarg;
                break;
            case 'c':
                sim_callid = optarg;
                break;
            case 'i':
                sim_ip = optarg;
                break;
            case 'p':
                port = atoi( optarg );
                break;
            case 'f':
                fvalue = optarg;
                break;
            case 's':
                local_password = optarg;
                break;
            case 'a':
                remote_ip = optarg;
                break;
            case 'b':
                local_ip = optarg;
                break;
            case 'j':
                local_port = atoi( optarg );
                break;
            case 'd':
                remote_port = atoi( optarg );
                break;
            case 'l':
                local_nr = optarg;
                break;
            case 'g':
                local_name = optarg;
                break;
            case 'r':
                remote_nr = optarg;
                break;
            case 'e':
                remote_name = optarg;
                break;
            case '?':
                if ( isprint( optopt ) )
                {
                    fprintf( stderr, "Unknown option `-%c'.\n", optopt );
                    print_usage();
                }
                else
                {
                    fprintf( stderr, "Unknown option character `\\x%x'.\n", optopt );
                    print_usage();
                }
                return -1;
            default:
                print_usage();
                abort();
        }

    if ( fvalue == NULL )
    {
        fprintf( stderr, "Please specify input pcap file\n" );
        print_usage();
        return -1;
    }

    if ( ovalue == NULL )
    {
        fprintf( stderr, "Please specify an option\n" );
        print_usage();
        return -1;
    }

    for ( index = optind; index < argc; index++ )
        printf( "Non-option argument %s\n", argv[index] );

    if ( strncmp( ovalue, "simulate", 8 ) == 0 )
    {
        if ( sim_callid == NULL )
        {
            fprintf( stderr, "Please specify callID \n" );
            print_usage();
            return -1;
        }
        if ( sim_ip == NULL )
        {
            fprintf( stderr, "Please specify IP \n" );
            print_usage();
            return -1;
        }
        if ( remote_ip == NULL )
        {
            fprintf( stderr, "Please specify remote_ip \n" );
            print_usage();
            return -1;
        }
        if ( local_ip == NULL )
        {
            fprintf( stderr, "Please specify local_ip \n" );
            print_usage();
            return -1;
        }
    }

    if ( port == 0 )
    {
        port = 5060;
    }
    if ( remote_port == 0 )
    {
        remote_port = 5060;
    }
    if ( local_port == 0 )
    {
        local_port = 5060;
    }

    return 0;
}

int print_injection_file()
{
    FILE *file = fopen( injection_file_path, "w" );
    if ( file == NULL )
    {
        fprintf( stderr, "Could not open file for writing: %s\n", injection_file_path );
        return -1;
    }

    fprintf( file, "SEQUENTIAL\n" );
    if ( local_nr != NULL )
    {
        fprintf( file, "%s;", local_nr );
    }
    else
        fprintf( file, ";" );
    if ( local_name != NULL )
    {
        fprintf( file, "%s;", local_name );
    }
    else
        fprintf( file, ";" );
    if ( remote_nr != NULL )
    {
        fprintf( file, "%s;", remote_nr );
    }
    else
        fprintf( file, ";" );
    if ( remote_name != NULL )
    {
        fprintf( file, "%s;", remote_name );
    }
    else
        fprintf( file, ";" );
    if ( local_password != NULL && local_nr != NULL )
    {
        fprintf( file, "[authentication username=%s password=%s];", local_nr, local_password );
    }
    else
        fprintf( file, ";" );

    fclose( file );

    return 0;
}

int main( int argc, char **argv )
{
    pcap_t *fp;
    char errbuf[PCAP_ERRBUF_SIZE] = {0};
    int read_status;
    char *local_ip_string;
    char *local_port_string;
    char *remote_ip_string;
    char sipp_command[500] = {0};
    char getrtp[500] = {0};
    char tmp[256] = {0};
    char name[128] = {0}; 
    int sys_res;

    read_status = read_configs( argc, argv );
    if ( read_status < 0 )
        return 1;

    /* Open a capture file */
    if ( (fp = pcap_open_offline( fvalue, errbuf )) == NULL )
    {
        fprintf( stderr, "Couldn't open pcap file %s: %s\n", fvalue, errbuf );
        return 1;
    }

    /* Make Dir for files*/
    char *ptr = strrchr( fvalue, '/' );
    if ( ptr == NULL )
    {
        ptr = fvalue;
    }
    else
    {
        ptr += 1; /*1 is for the '/'*/
    }
    sscanf( ptr, "%127[^.].%*s", name );
    //printf( "FILE NAME IS (%s)\n", name );
    snprintf( tmp, sizeof( tmp ), "case_%s", name );
    mkdir( tmp, 0777 );

    snprintf( injection_file_path, sizeof(injection_file_path), "./%s/%s", tmp, INJECTION );
    snprintf( scenario_file_path,  sizeof(scenario_file_path),  "./%s/%s", tmp, SCENARIO );
    snprintf( script_file_path,    sizeof(script_file_path),    "./%s/%s", tmp, SCRIPT );
    snprintf( rtp_file_path,       sizeof(rtp_file_path),       "./%s/%s", tmp, RTP );

    datalink = pcap_datalink( fp );

    if ( strncmp( ovalue, "listips", 7 ) == 0 )
    {
        //list all IPs in the file

        // read and dispatch packets until EOF is reached
        if ( pcap_loop( fp, 0, packet_handler_ips, NULL ) < 0 )
        {
            fprintf( stderr, "Could not handle the pcap packets: %s", pcap_geterr( fp ) );
            return 1;
        }

        //printf("******************* Available IP addresses **********************\n");
        print_ipnodes( ip_addresses );
        //printf("******************************************************************\n");

    }
    else if ( strncmp( ovalue, "listcallids", 12 ) == 0 )
    {
        //list all callids in the file

        // read and dispatch packets until EOF is reached
        if ( pcap_loop( fp, 0, packet_handler_callids, NULL ) < 0 )
        {
            fprintf( stderr, "Could not handle the pcap packets: %s", pcap_geterr( fp ) );
            return 1;
        }

        //printf("********************** Available Call IDs ************************\n");
        print_stringnodes( callids );
        //printf("******************************************************************\n");

    }
    else if ( strncmp( ovalue, "simulate", 8 ) == 0 )
    {
        //generate a sipp scenario using the given pcap file

        last_reply = malloc( sizeof(char) * 3 );
        strcpy( last_reply, "000" );

        scenario_file = fopen( scenario_file_path, "w" );
        if ( scenario_file == NULL )
        {
            fprintf( stderr, "Could not open file for writing: %s\n", scenario_file_path );
            return -1;
        }

        fprintf( scenario_file, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\" ?>\n"
                 "<!DOCTYPE scenario SYSTEM \"sipp.dtd\">\"\"\n"
                 "<scenario name=\"pcap2sipp generated scenario\">\n\n" );

        // read and dispatch packets until EOF is reached
        if ( pcap_loop( fp, 0, packet_handler_simulate, NULL ) < 0 )
        {
            fprintf( stderr, "Could not handle the pcap packets: %s", pcap_geterr( fp ) );
            return 1;
        }

        //printf( "********************** Generating simulation files *************************\n" );
        fprintf( scenario_file, "</scenario>" );
        fflush( scenario_file );
        fclose( scenario_file );

        sdp_millisecs = 1000 * (sdp_end_time - sdp_start_time);

        if ( print_injection_file() < 0 )
            return 1;

        if ( local_ip != NULL )
        {
            local_ip_string = malloc( sizeof(char) * strlen( local_ip ) );
            if ( local_ip_string == NULL )
            {
                fprintf( stderr, "Cannot allocate memory\n" );
                return 1;
            }

            sprintf( local_ip_string, "-i %s", local_ip );
        }
        else
        {
            local_ip_string = malloc( sizeof(char) );
            if ( local_ip_string == NULL )
            {
                fprintf( stderr, "Cannot allocate memory\n" );
                return 1;
            }

            *local_ip_string = '\0';
        };
        if ( local_port != 0 )
        {
            local_port_string = malloc( sizeof(char) * 6 );
            if ( local_port_string == NULL )
            {
                fprintf( stderr, "Cannot allocate memory\n" );
                return 1;
            }

            sprintf( local_port_string, "-p %d", local_port );
        }
        else
        {
            local_port_string = malloc( sizeof(char) );
            if ( local_port_string == NULL )
            {
                fprintf( stderr, "Cannot allocate memory\n" );
                return 1;
            }

            *local_port_string = '\0';
        };
        if ( remote_ip != NULL )
        {
            if ( remote_port != 0 )
            {
                remote_ip_string = malloc( sizeof(char) * 11 );
                if ( remote_ip_string == NULL )
                {
                    fprintf( stderr, "Cannot allocate memory\n" );
                    return 1;
                }

                sprintf( remote_ip_string, " %s:%d", remote_ip, remote_port );
            }
            else
            {
                remote_ip_string = malloc( sizeof(char) * (strlen( remote_ip )) );
                if ( remote_ip_string == NULL )
                {
                    fprintf( stderr, "Cannot allocate memory\n" );
                    return 1;
                }

                sprintf( remote_ip_string, " %s", remote_ip );
            }
        }
        else
        {
            remote_ip_string = malloc( sizeof(char) );
            if ( remote_ip_string == NULL )
            {
                fprintf( stderr, "Cannot allocate memory\n" );
                return 1;
            }

            remote_ip_string = '\0';
        }

        //extract the rtp file to be used in the scenario
        sprintf( getrtp, "tcpdump -r %s -s 0 -w %s 'src port %d' > /dev/null 2>&1",
                 fvalue, rtp_file_path, sdp_port );
        sys_res = system( getrtp );
        if ( sys_res < 0 )
        {
            fprintf( stderr, "The following comand generated an error, please check: %s\n", getrtp );
        }
        //printf( "The RTP file was generated. Path: %s \n", rtp_file_path );

        //sprintf( sipp_command, "rm -f /tmp/*.log; sipp -sf %s -inf %s %s %s %s -m 1 -trace_msg -d %d",
        //         scenario_file_path, injection_file_path, local_ip_string, local_port_string, remote_ip_string, sdp_millisecs );
        //printf( "All necessary data was succesfully generated. You can now run sipp with command: \n%s\n", sipp_command );

        script_file = fopen( script_file_path, "w" );
        if ( script_file == NULL )
        {
            fprintf( stderr, "Could not open file for writing: %s\n", script_file_path );
            return -1;
        }

        if ( need_remote_number > 0 )
        {
            sprintf( sipp_command, "#!/bin/bash\n"
                                    "\n"
                                    "PORT=\"5060\"\n"
                                    "\n"
                                    "if [ $# -lt 2 ]; then\n"
                                    "    echo \"Please input DUT IP and NUMBER\"\n"
                                    "    exit\n"
                                    "fi\n"
                                    "REMOTE_IP=$1\n"
                                    "REMOTE_NUMBER=$2\n"
                                    "\n"
                                    "IP=`ip -o -4 addr show up primary scope global | awk 'NR==1{print $4}'| cut -d/ -f1`\n"
                                    "echo \"Local IP is $IP\"\n"
                                    "\n"
                                    "sed -i \"s/6666/$REMOTE_NUMBER/g\" injection.csv\n" 
                                    "\n"
                                    "rm -f *.log \n"
                                    "\n"
                                    "sipp -sf scenario.xml -inf injection.csv -i $IP -p $PORT $REMOTE_IP -m 1 -trace_msg -d %d\n"
                                    "sed -i \"s/$REMOTE_NUMBER/6666/g\" injection.csv\n", sdp_millisecs );
        }
        else
        {
            sprintf( sipp_command, "#!/bin/bash\n"
                                    "\n"
                                    "PORT=\"5060\"\n"
                                    "\n"
                                    "if [ $# -lt 1 ]; then\n"
                                    "    echo \"Please input DUT IP\"\n"
                                    "    exit\n"
                                    "fi\n"
                                    "REMOTE_IP=$1\n"
                                    "\n"
                                    "IP=`ip -o -4 addr show up primary scope global | awk 'NR==1{print $4}'| cut -d/ -f1`\n"
                                    "echo \"Local IP is $IP\"\n"
                                    "\n"
                                    "rm -f *.log \n"
                                    "\n"
                                    "sipp -sf scenario.xml -inf injection.csv -i $IP -p $PORT $REMOTE_IP -m 1 -trace_msg -d %d\n", sdp_millisecs );
        }

        fprintf( script_file, sipp_command );

        fflush( script_file );
        fclose( script_file );
        chmod( script_file_path, 0777 );
        //run sipp command
        //system(sipp_command);
    }

    return 0;
}
