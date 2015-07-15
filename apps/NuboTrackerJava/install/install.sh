#!/bin/sh

APP_NAME=nubo-tracker
APP_JAR=NuboTrackerJava
# Kurento Java Tutorial 2 - Magic Mirror installator for Ubuntu 14.04
if [ `id -u` -ne 0 ]; then
    echo ""
    echo "Only root can start Kurento"
    echo ""
    exit 1
fi

APP_HOME=$(dirname $(readlink -f $0))

# Install binaries
install -o root -g root -m 755 $APP_HOME/demo-startup.sh /etc/init.d/$APP_NAME
mkdir -p /var/lib/kurento
install -o root -g root $APP_HOME/$APP_JAR.jar /var/lib/kurento/

# enable demo at startup
# update-rc.d $APP_NAME defaults

# start demo
/etc/init.d/$APP_NAME restart
