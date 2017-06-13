#!/bin/sh
cpack

wget https://download.vcmi.eu/deploy_rsa_CHANGEME.enc
touch /tmp/deploy_rsa_CHANGEME
chmod 600 /tmp/deploy_rsa_CHANGEME
openssl aes-256-cbc -K $encrypted_1d30f79f8582_key -iv $encrypted_1d30f79f8582_iv -in deploy_rsa_CHANGEME.enc -out /tmp/deploy_rsa_CHANGEME -d
eval "$(ssh-agent -s)"
ssh-add /tmp/deploy_rsa_CHANGEME

sftp -r -o StrictHostKeyChecking=no travis@beholder.vcmi.eu <<< "put $VCMI_PACKAGE_FILE_NAME.dmg /incoming/$VCMI_PACKAGE_FILE_NAME.dmg"
