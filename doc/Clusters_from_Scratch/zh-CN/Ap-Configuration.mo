��    ,      |  ;   �      �  k	  �     5  #  O  �   s  �  i  |    �   �  9    �   F    "  L   ?     �     �     �    �     �     �     �               "     >  �  Q  ?     Z  H  �   �  	   �  	   �     �     �  ?   �  >   �  p  9  D   �  Q   �  �  A  M   A!  U   �!  Z   �!  V   @"  O   �"  P   �"  *   8#    c#  k	  u$     �-  #  �-  �   /  �  0  |  �1  �   03  9  �3  �   �4    �5  L   �6     87     ?7     R7    _7     c8     v8     �8     �8     �8     �8     �8  r  �8  3   Z:  [  �:  �   �;     �<     �<     �<     �<  ?   �<  >   =    X=  9   x>  -   �>  �  �>  0   �@  K   �@  B   A  6   aA  S   �A  %   �A  $   B     "      +                        '                              
                                                    !   ,      *                         %   &   	   $   (            #           )          
[root@pcmk-1 ~]# crm configure show
node pcmk-1
node pcmk-2
primitive WebData ocf:linbit:drbd \
        params drbd_resource="wwwdata" \
        op monitor interval="60s"
primitive WebFS ocf:heartbeat:Filesystem \
        params device="/dev/drbd/by-res/wwwdata" directory="/var/www/html" fstype=”gfs2”
primitive WebSite ocf:heartbeat:apache \
        params configfile="/etc/httpd/conf/httpd.conf" \
        op monitor interval="1min"
primitive ClusterIP ocf:heartbeat:IPaddr2 \
        params ip=”192.168.122.101” cidr_netmask=”32” clusterip_hash=”sourceip” \
        op monitor interval="30s"
primitive dlm ocf:pacemaker:controld \
        op monitor interval="120s"
primitive gfs-control ocf:pacemaker:controld \
   params daemon=”gfs_controld.pcmk” args=”-g 0” \
        op monitor interval="120s"
primitive rsa-fencing stonith::external/ibmrsa \
        params hostname=”pcmk-1 pcmk-2" ipaddr=192.168.122.31 userid=mgmt passwd=abc123 type=ibm \
        op monitor interval="60s"
ms WebDataClone WebData \
        meta master-max="2" master-node-max="1" clone-max="2" clone-node-max="1" notify="true"
clone Fencing rsa-fencing 
clone WebFSClone WebFS
clone WebIP ClusterIP  \
        meta globally-unique=”true” clone-max=”2” clone-node-max=”2”
clone WebSiteClone WebSite
clone dlm-clone dlm \
        meta interleave="true"
clone gfs-clone gfs-control \
        meta interleave="true"
colocation WebFS-with-gfs-control inf: WebFSClone gfs-clone
colocation WebSite-with-WebFS inf: WebSiteClone WebFSClone
colocation fs_on_drbd inf: WebFSClone WebDataClone:Master
colocation gfs-with-dlm inf: gfs-clone dlm-clone
colocation website-with-ip inf: WebSiteClone WebIP
order WebFS-after-WebData inf: WebDataClone:promote WebFSClone:start
order WebSite-after-WebFS inf: WebFSClone WebSiteClone
order apache-after-ip inf: WebIP WebSiteClone
order start-WebFS-after-gfs-control inf: gfs-clone WebFSClone
order start-gfs-after-dlm inf: dlm-clone gfs-clone
property $id="cib-bootstrap-options" \
        dc-version="1.0.5-462f1569a43740667daf7b0f6b521742e9eb8fa7" \
        cluster-infrastructure="openais" \
        expected-quorum-votes=”2” \
        stonith-enabled=”true” \
        no-quorum-policy="ignore"
rsc_defaults $id="rsc-options" \
        resource-stickiness=”100”
 
node pcmk-1
node pcmk-2
 
primitive ClusterIP ocf:heartbeat:IPaddr2 \
        params ip=”192.168.122.101” cidr_netmask=”32” clusterip_hash=”sourceip” \
        op monitor interval="30s"
clone WebIP ClusterIP  
        meta globally-unique=”true” clone-max=”2” clone-node-max=”2”
 
primitive WebData ocf:linbit:drbd \
        params drbd_resource="wwwdata" \
        op monitor interval="60s"
ms WebDataClone WebData \
        meta master-max="2" master-node-max="1" clone-max="2" clone-node-max="1" notify="true"
 
primitive WebFS ocf:heartbeat:Filesystem \
        params device="/dev/drbd/by-res/wwwdata" directory="/var/www/html" fstype=”gfs2”
clone WebFSClone WebFS
colocation WebFS-with-gfs-control inf: WebFSClone gfs-clone
colocation fs_on_drbd inf: WebFSClone WebDataClone:Master
order WebFS-after-WebData inf: WebDataClone:promote WebFSClone:start
order start-WebFS-after-gfs-control inf: gfs-clone WebFSClone
 
primitive WebSite ocf:heartbeat:apache \
        params configfile="/etc/httpd/conf/httpd.conf" \
        op monitor interval="1min"
clone WebSiteClone WebSite
colocation WebSite-with-WebFS inf: WebSiteClone WebFSClone
colocation website-with-ip inf: WebSiteClone WebIP
order apache-after-ip inf: WebIP WebSiteClone
order WebSite-after-WebFS inf: WebFSClone WebSiteClone
 
primitive dlm ocf:pacemaker:controld \
        op monitor interval="120s"
clone dlm-clone dlm \
        meta interleave="true
 
primitive gfs-control ocf:pacemaker:controld \
   params daemon=”gfs_controld.pcmk” args=”-g 0” \
        op monitor interval="120s"
clone gfs-clone gfs-control \
        meta interleave="true"
colocation gfs-with-dlm inf: gfs-clone dlm-clone
order start-gfs-after-dlm inf: dlm-clone gfs-clone
 
primitive rsa-fencing stonith::external/ibmrsa \
        params hostname=”pcmk-1 pcmk-2" ipaddr=192.168.122.31 userid=mgmt passwd=abc123 type=ibm \
        op monitor interval="60s"
clone Fencing rsa-fencing
 
property $id="cib-bootstrap-options" \
        dc-version="1.0.5-462f1569a43740667daf7b0f6b521742e9eb8fa7" \
        cluster-infrastructure="openais" \
        expected-quorum-votes=”2” \
        stonith-enabled=”true” \
        no-quorum-policy="ignore"
 
rsc_defaults $id="rsc-options" \
        resource-stickiness=”100”
 Apache Cluster Filesystem Cluster Options Cluster filesystems like GFS2 require a lock manager. This service starts the daemon that provides user-space applications (such as the GFS2 daemon) with access to the in-kernel lock manager. Since we need it to be available on all nodes in the cluster, we have it cloned. Configuration Recap DRBD - Shared Storage Default Options Distributed lock manager Fencing Final Cluster Configuration GFS control daemon GFS2 also needs a user-space/kernel bridge that runs on every node. So here we have another clone, however this time we must also specify that it can only run on machines that are also running the DLM (colocation constraint) and that it can only be started after the DLM is running (order constraint). Additionally, the gfs-control clone should only care about the DLM instances it is paired with, so we need to set the interleave option. Here we configure cluster options that apply to every resource. Here we define the DRBD service and specify which DRBD resource (from drbd.conf) it should manage. We make it a master/slave resource and, in order to have an active/active setup, allow both instances to be promoted by specifying master-max=2. We also set the notify option so that the cluster will tell DRBD agent when it’s peer changes state. Lastly we have the actual service, Apache. We need only tell the cluster where to find it’s main configuration file and restrict it to running on nodes that have the required filesystem mounted and the IP address active. Node List Resources Service Address TODO: Add text here TODO: Confirm <literal>interleave</literal> is no longer needed TODO: The RA should check for globally-unique=true when cloned The cluster filesystem ensures that files are read and written correctly. We need to specify the block device (provided by DRBD), where we want it mounted and that we are using GFS2. Again it is a clone because it is intended to be active on both nodes. The additional constraints ensure that it can only be started on nodes with active gfs-control and drbd instances. The list of cluster nodes is automatically populated by the cluster. This is where the cluster automatically stores some information about the cluster Users of the services provided by the cluster require an unchanging address with which to access it. Additionally, we cloned the address so it will be active on both nodes. An iptables rule (created as part of the resource agent) is used to ensure that each request only processed by one of the two clone instances. The additional meta options tell the cluster that we want two instances of the clone (one “request bucket” for each node) and that if one node fails, then the remaining node should hold both. and where the admin can set options that control the way the cluster operates cluster-infrastructure - the cluster infrastructure being used (heartbeat or openais) dc-version - the version (including upstream source-code hash) of Pacemaker used on the DC expected-quorum-votes - the maximum number of nodes expected to be part of the cluster no-quorum-policy=ignore - Ignore loss of quorum and continue to host resources. resource-stickiness - Specify the aversion to moving resources to other machines stonith-enabled=true - Make use of STONITH Project-Id-Version: 0
POT-Creation-Date: 2010-12-15T23:32:36
PO-Revision-Date: 2010-12-15 23:34+0800
Last-Translator: Charlie Chen <laneovcc@gmail.com>
Language-Team: None
Language: 
MIME-Version: 1.0
Content-Type: text/plain; charset=UTF-8
Content-Transfer-Encoding: 8bit
 
[root@pcmk-1 ~]# crm configure show
node pcmk-1
node pcmk-2
primitive WebData ocf:linbit:drbd \
        params drbd_resource="wwwdata" \
        op monitor interval="60s"
primitive WebFS ocf:heartbeat:Filesystem \
        params device="/dev/drbd/by-res/wwwdata" directory="/var/www/html" fstype=”gfs2”
primitive WebSite ocf:heartbeat:apache \
        params configfile="/etc/httpd/conf/httpd.conf" \
        op monitor interval="1min"
primitive ClusterIP ocf:heartbeat:IPaddr2 \
        params ip=”192.168.122.101” cidr_netmask=”32” clusterip_hash=”sourceip” \
        op monitor interval="30s"
primitive dlm ocf:pacemaker:controld \
        op monitor interval="120s"
primitive gfs-control ocf:pacemaker:controld \
   params daemon=”gfs_controld.pcmk” args=”-g 0” \
        op monitor interval="120s"
primitive rsa-fencing stonith::external/ibmrsa \
        params hostname=”pcmk-1 pcmk-2" ipaddr=192.168.122.31 userid=mgmt passwd=abc123 type=ibm \
        op monitor interval="60s"
ms WebDataClone WebData \
        meta master-max="2" master-node-max="1" clone-max="2" clone-node-max="1" notify="true"
clone Fencing rsa-fencing 
clone WebFSClone WebFS
clone WebIP ClusterIP  \
        meta globally-unique=”true” clone-max=”2” clone-node-max=”2”
clone WebSiteClone WebSite
clone dlm-clone dlm \
        meta interleave="true"
clone gfs-clone gfs-control \
        meta interleave="true"
colocation WebFS-with-gfs-control inf: WebFSClone gfs-clone
colocation WebSite-with-WebFS inf: WebSiteClone WebFSClone
colocation fs_on_drbd inf: WebFSClone WebDataClone:Master
colocation gfs-with-dlm inf: gfs-clone dlm-clone
colocation website-with-ip inf: WebSiteClone WebIP
order WebFS-after-WebData inf: WebDataClone:promote WebFSClone:start
order WebSite-after-WebFS inf: WebFSClone WebSiteClone
order apache-after-ip inf: WebIP WebSiteClone
order start-WebFS-after-gfs-control inf: gfs-clone WebFSClone
order start-gfs-after-dlm inf: dlm-clone gfs-clone
property $id="cib-bootstrap-options" \
        dc-version="1.0.5-462f1569a43740667daf7b0f6b521742e9eb8fa7" \
        cluster-infrastructure="openais" \
        expected-quorum-votes=”2” \
        stonith-enabled=”true” \
        no-quorum-policy="ignore"
rsc_defaults $id="rsc-options" \
        resource-stickiness=”100”
 
node pcmk-1
node pcmk-2
 
primitive ClusterIP ocf:heartbeat:IPaddr2 \
        params ip=”192.168.122.101” cidr_netmask=”32” clusterip_hash=”sourceip” \
        op monitor interval="30s"
clone WebIP ClusterIP  
        meta globally-unique=”true” clone-max=”2” clone-node-max=”2”
 
primitive WebData ocf:linbit:drbd \
        params drbd_resource="wwwdata" \
        op monitor interval="60s"
ms WebDataClone WebData \
        meta master-max="2" master-node-max="1" clone-max="2" clone-node-max="1" notify="true"
 
primitive WebFS ocf:heartbeat:Filesystem \
        params device="/dev/drbd/by-res/wwwdata" directory="/var/www/html" fstype=”gfs2”
clone WebFSClone WebFS
colocation WebFS-with-gfs-control inf: WebFSClone gfs-clone
colocation fs_on_drbd inf: WebFSClone WebDataClone:Master
order WebFS-after-WebData inf: WebDataClone:promote WebFSClone:start
order start-WebFS-after-gfs-control inf: gfs-clone WebFSClone
 
primitive WebSite ocf:heartbeat:apache \
        params configfile="/etc/httpd/conf/httpd.conf" \
        op monitor interval="1min"
clone WebSiteClone WebSite
colocation WebSite-with-WebFS inf: WebSiteClone WebFSClone
colocation website-with-ip inf: WebSiteClone WebIP
order apache-after-ip inf: WebIP WebSiteClone
order WebSite-after-WebFS inf: WebFSClone WebSiteClone
 
primitive dlm ocf:pacemaker:controld \
        op monitor interval="120s"
clone dlm-clone dlm \
        meta interleave="true
 
primitive gfs-control ocf:pacemaker:controld \
   params daemon=”gfs_controld.pcmk” args=”-g 0” \
        op monitor interval="120s"
clone gfs-clone gfs-control \
        meta interleave="true"
colocation gfs-with-dlm inf: gfs-clone dlm-clone
order start-gfs-after-dlm inf: dlm-clone gfs-clone
 
primitive rsa-fencing stonith::external/ibmrsa \
        params hostname=”pcmk-1 pcmk-2" ipaddr=192.168.122.31 userid=mgmt passwd=abc123 type=ibm \
        op monitor interval="60s"
clone Fencing rsa-fencing
 
property $id="cib-bootstrap-options" \
        dc-version="1.0.5-462f1569a43740667daf7b0f6b521742e9eb8fa7" \
        cluster-infrastructure="openais" \
        expected-quorum-votes=”2” \
        stonith-enabled=”true” \
        no-quorum-policy="ignore"
 
rsc_defaults $id="rsc-options" \
        resource-stickiness=”100”
 Apache 集群文件系统 集群选项 像GFS2集群文件系统需要一个锁管理。该服务启动守护进程，提供了访问内核中的锁管理器的用户空间应用程序（如GFS2守护进程）。因为我们需要它在集群中的所有可用节点中运行，我们把它clone。 配置扼要重述 DRBD - 共享存储 默认选项 分布式锁控制器 隔离 最终的集群配置文件 GFS 控制守护进程 GFS2还需要一个user-space到kernel的桥梁，每个节点上要运行。所以在这里我们还有一个clone，但是这一次我们还必须指定它只能运行在有DLM的机器上（colocation 约束），它只能在DLM后启动 （order约束）。此外，gfs-control clone应该只关系与其配对的DLM实例，所以我们还要设置interleave 选项 这里我们设置所有资源共用的集群选项 在这里，我们定义了DRBD技术服务，并指定DRBD应该管理的资源（从drbd.conf）。我们让它作为主/从资源，并且为了active/active，用设置master-max=2来允许两者都晋升为master。我们还可以设置通知选项，这样，当时集群的节点的状态发生改变时，该集群将告诉DRBD的agent。  最后我们有了真正的服务，Apache，我们只需要告诉集群在哪里可以找到它的主配置文件，并限制其只在挂载了文件系统和有可用IP节点上运行 节点列表 资源 服务地址 TODO: Add text here TODO: Confirm <literal>interleave</literal> is no longer needed TODO: The RA should check for globally-unique=true when cloned 群集文件系统可确保文件读写正确。我们需要指定我们想挂载并使用GFS2的块设备（由DRBD提供）。这又是一个clone，因为它的目的是在两个节点上都可用。这些额外的限制确保它只在有gfs-control和drbd 实例的节点上运行。 这个列表中的集群节点是集群自动添加的。 这是集群自动存储集群信息的地方 用户需要一个不变的地址来访问集群所提供的服务。此外，我们clone了地址，以便在两个节点上都使用这个IP。一个iptables规则（resource agent的一部分）是用来确保每个请求只能由两个节点中的某一个处理。这些额外的集群选项告诉我们想要两个clone（每个节点一个“请求桶”）实例，如果一个节点失效，那么剩下的节点处理这两个请求桶。 以及管理员设置集群操作的方法选项 集群-基层 - 集群使用的基层软件 (heartbeat or openais/corosync) dc-version - DC使用的Pacemaker的版本(包括源代码的hash) expected-quorum-votes - 预期的集群最大成员数 no-quorum-policy=ignore - 忽略达不到法定人数的情况，继续运行资源 resource-stickiness - 资源粘稠值 stonith-enabled=true - 使用STONITH 