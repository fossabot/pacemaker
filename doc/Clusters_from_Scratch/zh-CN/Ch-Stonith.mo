��          �   %   �      P  �  Q  
  9  �   S  G   �  �   ,  z        �     �  �   �  �   D     �  #   �  �     v   �    &  �   6  �   �  �   �  �   #  &  �  T   �  V   9  "   �     �  %   �    �  �     
  �  �   &  5   �&  �   �&  \   �'     �'     �'  �   �'  �   �(     	)  (   )  �   9)  R   �)  �   *  s   �*  �   f+  �   #,  y   �,  �   <-  H   .  V   f.  %   �.     �.  %   �.                                   
                                                     	                                     
[root@pcmk-1 ~]# <userinput>crm </userinput>
crm(live)# <userinput>cib new stonith</userinput>
INFO: stonith shadow CIB created
crm(stonith)# <userinput>configure primitive rsa-fencing stonith::external/ibmrsa \</userinput>
<userinput>        params hostname=”pcmk-1 pcmk-2" ipaddr=192.168.122.31 userid=mgmt passwd=abc123 type=ibm \</userinput>
<userinput>        op monitor interval="60s"</userinput>
crm(stonith)# <userinput>configure clone Fencing rsa-fencing</userinput>
 
crm(stonith)# <userinput>configure property stonith-enabled="true"</userinput>
crm(stonith)# <userinput>configure show</userinput>
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
<emphasis>primitive rsa-fencing stonith::external/ibmrsa \</emphasis>
<emphasis> params hostname=”pcmk-1 pcmk-2" ipaddr=192.168.122.31 userid=mgmt passwd=abc123 type=ibm \</emphasis>
<emphasis> op monitor interval="60s"</emphasis>
ms WebDataClone WebData \
        meta master-max="2" master-node-max="1" clone-max="2" clone-node-max="1" notify="true"
<emphasis>clone Fencing rsa-fencing </emphasis>
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
        <emphasis>stonith-enabled="true"</emphasis> \
        no-quorum-policy="ignore"
rsc_defaults $id="rsc-options" \
        resource-stickiness=”100”
 
stonith -t external/ibmrsa -n
[root@pcmk-1 ~]# <userinput>stonith -t external/ibmrsa -n</userinput>
hostname  ipaddr  userid  passwd  type
 And finally, since we disabled it earlier, we need to re-enable STONITH Assuming we have an IBM BladeCenter containing our two nodes and the management interface is active on 192.168.122.31, then we would chose the external/ibmrsa driver in step 2 and obtain the following list of parameters Assuming we know the username and password for the management interface, we would create a STONITH resource with the shell Configure STONITH Configuring STONITH Create a clone from the primitive resource if the device can shoot more than one node<emphasis> and supports multiple simultaneous connections</emphasis>. Create a file called stonith.xml containing a primitive resource with a class of stonith, a type of {type} and a parameter for each of the values returned in step 2 Example Find the correct driver: stonith -L Hopefully the developers chose names that make sense, if not you can query for some additional information by finding an active cluster node and running: It is crucial that the STONITH device can allow the cluster to differentiate between a node failure and a network one. Just because a node is unresponsive, this doesn’t mean it isn’t accessing your data. The only way to be 100% sure that your data is safe, is to use STONITH so we can be certain that the node is truly offline, before allowing the data to be accessed from another node. Likewise, any device that relies on the machine being active (such as SSH-based “devices” used during testing) are inappropriate. STONITH also has a role to play in the event that a clustered service cannot be stopped. In this case, the cluster uses STONITH to force the whole node offline, thereby making it safe to start the service elsewhere. STONITH is an acronym for Shoot-The-Other-Node-In-The-Head and it protects your data from being corrupted by rouge nodes or concurrent access. Since every device is different, the parameters needed to configure it will vary. To find out the parameters required by the device: stonith -t {type} -n The biggest mistake people make in choosing a STONITH device is to use remote power switch (such as many onboard IMPI controllers) that shares power with the node it controls. In such cases, the cluster cannot be sure if the node is really offline, or active and suffering from a network fault. The output should be XML formatted text containing additional parameter descriptions Upload it into the CIB using cibadmin: cibadmin -C -o resources --xml-file stonith.xml What STONITH Device Should You Use Why You Need STONITH lrmadmin -M stonith {type} pacemaker
 Project-Id-Version: 0
POT-Creation-Date: 2010-12-15T23:32:37
PO-Revision-Date: 2010-12-16 00:38+0800
Last-Translator: Charlie Chen <laneovcc@gmail.com>
Language-Team: None
Language: 
MIME-Version: 1.0
Content-Type: text/plain; charset=UTF-8
Content-Transfer-Encoding: 8bit
 
[root@pcmk-1 ~]# <userinput>crm </userinput>
crm(live)# <userinput>cib new stonith</userinput>
INFO: stonith shadow CIB created
crm(stonith)# <userinput>configure primitive rsa-fencing stonith::external/ibmrsa \</userinput>
<userinput>        params hostname=”pcmk-1 pcmk-2" ipaddr=192.168.122.31 userid=mgmt passwd=abc123 type=ibm \</userinput>
<userinput>        op monitor interval="60s"</userinput>
crm(stonith)# <userinput>configure clone Fencing rsa-fencing</userinput>
 
crm(stonith)# <userinput>configure property stonith-enabled="true"</userinput>
crm(stonith)# <userinput>configure show</userinput>
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
<emphasis>primitive rsa-fencing stonith::external/ibmrsa \</emphasis>
<emphasis> params hostname=”pcmk-1 pcmk-2" ipaddr=192.168.122.31 userid=mgmt passwd=abc123 type=ibm \</emphasis>
<emphasis> op monitor interval="60s"</emphasis>
ms WebDataClone WebData \
        meta master-max="2" master-node-max="1" clone-max="2" clone-node-max="1" notify="true"
<emphasis>clone Fencing rsa-fencing </emphasis>
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
        <emphasis>stonith-enabled="true"</emphasis> \
        no-quorum-policy="ignore"
rsc_defaults $id="rsc-options" \
        resource-stickiness=”100”
 
stonith -t external/ibmrsa -n
[root@pcmk-1 ~]# <userinput>stonith -t external/ibmrsa -n</userinput>
hostname  ipaddr  userid  passwd  type
 最后，我们要重新打开之前禁用的STONITH: 假设我们有一个 包含两个节点的IBM BladeCenter，控制界面的IP是192.168.122.31,然后我们选择 external/ibmrsa作为驱动，然后配置下面列表当中的参数。 假设我们知道管理界面的用户名和密码，我们要创建一个STONITH的资源: 配置 STONITH 配置STONITH 如果这个设备可以击杀多个设备并且支持从多个节点连接过来，那我们从这个原始资源创建一个克隆。 创建stonith.xml文件 包含了一个原始的源，它定义了资stonith类下面的某个type和这个type所需的参数。 例子 找到正确的STONITH驱动: stonith -L 希望开发者选择了合适的名称，如果不是这样，你可以在活动的机器上面执行以下命令来获得更多信息。 重要的一点是STONITH设备可以让集群区分节点故障和网络故障。 因为如果一个节点没有相应，但并不代表它没有在操作你的数据，100%保证数据安全的做法就是在允许另外一个节点操作数据之前，使用STONITH来保证节点真的下线了。 同样地， 任何依靠可用节点的设备（比如测试用的基于SSH的“设备”）都是不适当的。 STONITH另外一个用场是在当集群服务无法停止的时候。这个时候，集群可以用STONITH来强制使节点下线，从而可以安全的得在其他地方启动服务。 STONITH 是爆其他节点的头（ Shoot-The-Other-Node-In-The-Head）的缩写，它能保护你的数据不被不正常的节点破坏或是并发写入。 因为设备的不同, 配置的参数也不一样。 想看设备所需设置的参数，可以用: stonith -t {type} -n 人们常常犯得一个错误就是选择远程电源开关作为STONITH设备(比如许多主板自带的IPMI控制器) 。在那种情况下，集群不能分辨节点是真正的下线了，还是网络无法连通了。 输出应该是XML格式的文本文件，它包含了更详细的描述 使用cibadmin来更新CIB配置文件:cibadmin -C -o resources --xml-file stonith.xml 你该用什么样的STONITH设备。 为什么需要 STONITH lrmadmin -M stonith {type} pacemaker
 