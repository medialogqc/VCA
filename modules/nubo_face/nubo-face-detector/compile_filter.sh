
#!/bin/sh

#kurento-module-scaffold.sh example kms-example opencv_filter
mkdir -p build
cd build; cmake ../; sleep 5 ; make; sleep 5 ; cmake ../  -DGENERATE_JAVA_CLIENT_PROJECT=TRUE; sleep 5; make java_install
debuild -us -uc
