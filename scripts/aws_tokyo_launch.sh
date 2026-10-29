#!/usr/bin/env bash
# aws_tokyo_launch.sh — paste into AWS CloudShell (region ap-northeast-1, Tokyo) to create the market-making server.
# Lighter's API origin is in AWS Tokyo: 10 ms from there vs ~240 ms from home (measured 2026-10-29).
#
#   bash aws_tokyo_launch.sh            prepare: import the home box's SSH key, SSH-only firewall, find the Ubuntu image
#   bash aws_tokyo_launch.sh --launch   prepare + START the instance (billing starts: t3.medium ≈ $0.054/h ≈ $40/month on
#                                       demand; TYPE=c7i.large ≈ $80/month if CPU ever binds)
#
# Then on the home box:  ssh ubuntu@<public ip>  and follow docs/mm_live_runbook.md (scripts/vps_setup.sh; a NEW Lighter
# API key is created ON the server, never copied from home). Idempotent: an existing key pair / security group is reused.
set -euo pipefail
export AWS_DEFAULT_REGION=ap-northeast-1
TYPE="${TYPE:-t3.medium}"
HOME_IP="${HOME_IP:-91.184.223.122}"     # the home box's public IP (curl https://checkip.amazonaws.com)
PUB='ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIF/5ZuLd2QnjYVA0Q8fyAFJ0U8tJkliTqp90fh6qRHRU rithmic-mm'

aws ec2 describe-key-pairs --key-names rithmic-mm >/dev/null 2>&1 ||
    aws ec2 import-key-pair --key-name rithmic-mm --public-key-material "$(printf '%s' "$PUB" | base64 -w0)" >/dev/null
SG=$(aws ec2 describe-security-groups --group-names rithmic-mm-ssh --query 'SecurityGroups[0].GroupId' --output text 2>/dev/null ||
     aws ec2 create-security-group --group-name rithmic-mm-ssh --description 'SSH from the home box only' --query GroupId --output text)
aws ec2 authorize-security-group-ingress --group-id "$SG" --protocol tcp --port 22 --cidr "$HOME_IP/32" >/dev/null 2>&1 || true
AMI=$(aws ssm get-parameter --name /aws/service/canonical/ubuntu/server/24.04/stable/current/amd64/hvm/ebs-gp3/ami-id --query Parameter.Value --output text)
echo "key rithmic-mm · security group $SG (SSH from $HOME_IP only) · Ubuntu 24.04 $AMI · type $TYPE"

[ "${1:-}" = "--launch" ] || { echo "prepared — run again with --launch to start the server (billing starts)"; exit 0; }
ID=$(aws ec2 run-instances --image-id "$AMI" --instance-type "$TYPE" --key-name rithmic-mm --security-group-ids "$SG" \
     --block-device-mappings 'DeviceName=/dev/sda1,Ebs={VolumeSize=80,VolumeType=gp3,DeleteOnTermination=true}' \
     --tag-specifications 'ResourceType=instance,Tags=[{Key=Name,Value=rithmic-mm-tokyo}]' --query 'Instances[0].InstanceId' --output text)
aws ec2 wait instance-running --instance-ids "$ID"
IP=$(aws ec2 describe-instances --instance-ids "$ID" --query 'Reservations[0].Instances[0].PublicIpAddress' --output text)
echo "running: $ID  ssh ubuntu@$IP"
echo "latency to Lighter from here:"; for i in 1 2 3 4 5; do curl -s -o /dev/null -w '%{time_appconnect} %{time_starttransfer}\n' https://mainnet.zklighter.elliot.ai/api/v1/nextNonce?account_index=764037\&api_key_index=4; done
