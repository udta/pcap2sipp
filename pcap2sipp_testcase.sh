#!/bin/bash
#set -x

PCAP=$1

#####
#命令路径
PCAP2SIPP=/base/SourceCode/pcap2sipp/pcap2sipp

#####
#服务器IP
BSServerAddr=199.182.125.3
BSServerPort=5060

####
#进行SIPP测试时期望话机使用的号码
NUMBER="6666"
AuthID="6666"
#Password="1q2w3e"

#####
#34.pcapng有多个CALL ID
getCallIDs() {
   a=`$PCAP2SIPP -o listcallids -f $1`  
   b=${a//\*/=}
   
   for l in { $b } 
   do 
       if [[ $l =~ "@" ]]; then
          echo "$l"
       fi   
   done    
}

####
#获取DUT IP及端口
getIPs() {
  a=`$PCAP2SIPP -o listips -f $1 | grep -v $2`
  echo $a 
}


CIDS=`getCallIDs $PCAP`

echo "Call-ID(s):"
echo "$CIDS"
Addr=`getIPs $PCAP $BSServerAddr`
IP=`echo $Addr | awk -F":" '{print $1}'`
PORT=`echo $Addr | awk -F":" '{print $2}'`

echo ""
echo "DUT Addr: $IP:$PORT"

$PCAP2SIPP -o simulate -f $PCAP -c $CIDS -a $BSServerAddr -d $BSServerPort -i $IP -p $PORT -b 127.0.0.1 -r $NUMBER -e $AuthID  

###Below is for getting Phone's track
#$PCAP2SIPP -o simulate -f $PCAP -c $CIDS -i $BSServerAddr -p $BSServerPort -a $IP -d $PORT -b 127.0.0.1 -l $NUMBER -g $AuthID -s $Password
