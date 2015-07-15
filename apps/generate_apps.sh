#!/bin/sh

mkdir -p apps_install
mkdir -p ../output
cd NuboEarJava/; sh generate_zip.sh; cd ../
cd NuboFaceJava/; sh generate_zip.sh; cd ../
cd NuboFaceProfileJava/; sh generate_zip.sh; cd ../
cd NuboMouthJava/; sh generate_zip.sh; cd ../
cd NuboNoseJava/; sh generate_zip.sh; cd ../
cd NuboTrackerJava/; sh generate_zip.sh; cd ../
cd NuboEyeJava/; sh generate_zip.sh; cd ../
