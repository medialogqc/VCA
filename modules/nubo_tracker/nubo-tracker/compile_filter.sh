
#!/bin/sh

#kurento-module-scaffold.sh example kms-example opencv_filter
mkdir -p build
cd build; cmake ../ -DGENERATE_JAVA_CLIENT_PROJECT=TRUE; sleep 2;make; sleep 2; make java_install
debuild -us -uc
