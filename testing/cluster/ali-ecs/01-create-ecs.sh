#!/bin/bash

# 配置阿里云CLI使用的AccessKey和SecretKey
# 注意：实际使用时，请确保已通过环境变量或配置文件安全地设置了AccessKey和SecretKey

# 1. 设置变量
INSTANCE_NAME="pfs-test"

REGION=cn-wulanchabu

# 3. 创建VPC、VSwitch、SecurityGtoup
#echo "正在创建VPC..."
#VpcId=$(aliyun vpc CreateVpc --RegionId $REGION --CidrBlock 192.168.0.0/16 | jq -r .VpcId)
VpcId="vpc-0jlg5e7plzqrzqhhbbjuy"
#aliyun vpc DescribeVpcAttribute --RegionId $REGION --VpcId ${VpcId} --waiter expr='Status' to=Available > /dev/null 2>&1

#echo "正在创建VSwitch..."
#VSwitchId=$(aliyun vpc CreateVSwitch --CidrBlock 192.168.0.0/24 --VpcId ${VpcId} --ZoneId=cn-hangzhou-i | jq -r .VSwitchId)
VSwitchId="vsw-0jl41fu7050zhcjyeqk5m"
#echo "正在创建SecurityGtoup..."
#SecurityGroupId=$(aliyun ecs CreateSecurityGroup --RegionId cn-hangzhou --VpcId ${VpcId} | jq -r .SecurityGroupId)
SecurityGroupId="sg-0jl5xk0ua47rjz06v8i8"
#aliyun ecs AuthorizeSecurityGroup --RegionId cn-hangzhou --SecurityGroupId ${SecurityGroupId} --IpProtocol tcp --SourceCidrIp 0.0.0.0/0 --PortRange 22/22 > /dev/null 2>&1



#Amount:20, //创建数量为 20 台
#InstanceChargeType:"PostPaid",
#SpotStrategy:"SpotAsPriceGo", //表示抢占式实例的竞价策略为根据市场价格自动出价
#SpotDuration:1 //设置抢占式实例的保留时长为 1 小时

#InstanceType : 

# 4. 执行创建ECS实例的命令
echo "正在创建ECS实例..."
INSTANCE_ID_RAW=$(aliyun ecs RunInstances \
--RegionId $REGION \
--ImageId ubuntu_22_04_x64_20G_alibase_20241016.vhd \
--InstanceType ecs.e-c1m2.xlarge \
--SecurityGroupId ${SecurityGroupId} \
--VSwitchId ${VSwitchId} \
--InstanceName $INSTANCE_NAME \
--InstanceChargeType PostPaid \
--InternetMaxBandwidthOut 50 \
--Password pfstesT@123  \
--SystemDisk.Category cloud_essd \
--SystemDisk.Size 20 \
--DataDisk.1.Category cloud_essd \
--DataDisk.1.Size 30 \
--SpotStrategy "SpotAsPriceGo" \
--SpotDuration 1 \
--Amount 3 \
--HostName pfs  \
--UniqueSuffix true )

# 5. 提取InstanceId，用于后续打印状态
INSTANCE_ID=$(echo "$INSTANCE_ID_RAW" | jq -r '.InstanceIdSets.InstanceIdSet[]')

# 6. 休息20秒，等待ECS创建中...
echo "等待ECS创建中..."
sleep 20

# 7. 查询ECS状态
echo "查询ECS状态..."
INSTANCE_ID_QUOTED=$(printf '"%s"' "$INSTANCE_ID")
#aliyun ecs DescribeInstances \
#--RegionId $REGION \
#--InstanceIds "[${INSTANCE_ID_QUOTED}]" \
#--output cols=InstanceId,InstanceName,InstanceType,ImageId,Status rows=Instances.Instance[]


#aliyun ecs DescribeInstances --RegionId $REGION --output cols=InstanceId,InstanceName,InstanceType,ImageId,Status,PublicIpAddress.IpAddress[0]  rows=Instances.Instance[]

aliyun ecs DescribeInstances --RegionId $REGION --output cols=InstanceId,StartTime,PublicIpAddress.IpAddress[0],VpcAttributes.PrivateIpAddress.IpAddress[0],Status  rows=Instances.Instance[] num=true

# to release ecs, not work for running ecs
# aliyun ecs DeleteInstances --RegionId $REGION  --InstanceId.1 i-0jl8sx775or2z2hrvk89  --InstanceId.2 i-0jl8sx775or2z2hrvk87

