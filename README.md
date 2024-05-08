# r4r
Monorepo for everything linked to the r4r project

# Usage:
Make sure you are using a debian-based system with dpkg, apt and R installed. 
Everything should work fine without the installed R but this has not been tested. The only result should be that no R packages will get resolved due to execve fails.

For reproduction, docker is also required.

```
mkdir build;
cd build;
cmake -DCMAKE_BUILD_TYPE=Release ..;
make;
```
wait for the build to finish, should take around a minute

pick a program you want to test
```
mkdir run;
cd run;
../ptraceBasedTracer [program] < optional args >
```
The traced program input is closed.
stdout and stderr are redirected to stdout.txt and stderr.txt respectively.

wait untill the program terminates

stderr will be populated with potential errors.
stdout is currently mostly unused, no progress will be reported

accessedFiles.csv will be created. THe file contains a csv dump of all the detected filesystem dependencies.
 
report.txt contains some interesting statistics taken from the dependencies

 launch.sh is created. The script stores the launch environment of the program.
Input escaping is not properly implemented. You might have to manually change the launch arguments on the last line of the script.

buildDocker.sh contains a script which will
1. create a folder
2. copy relevant files into the folder so they can be transferred to an image
3. build a docker image  
4. delete the folder
5. run the built docker image in the current shell
6. inspect the container or run ./launch.sh 

Building the image will probably take long. Access to the internet is necessary. Just run the script and go for lunch.
171 apt packages took around 700 seconds
131 R packages took around 1500 seconds
Your mileage will vary but this should show just how long a build process can take.

In case of errors, delete the offending library/file from the dockerfile and re-run. It will still probably work due to system defaults.
