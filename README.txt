Building on Windows

	Install cmake 
	Install a build environment eg msys+http://www.drangon.org/mingw/ 
	Install libapr1-dev  (included in mingw devel from drangon mingw)
		
	sh
	mkdir diskBench-build		
	cmake -G "MSYS Makefiles" <pathToDiskBenchSrc> -DCMAKE_BUILD_TYPE=Release	
	make

Building on Linux	

	Install Build environment ie standard GCC and make
		CMake		
		libapr-1-dev
		libaio-dev
			

	mkdir diskBench-build
	cd diskBench-build
	cmake -G "Unix Makefiles"  <pathToDiskBenchSrc> -DCMAKE_BUILD_TYPE=Release	
	make
		
Running

Note: prebuilt linux is dynamically linked on ubuntu 12.04 and needs libaio and libapr-1


A test without any arguments will take around 30 minutes. 

A simple way to influence the run-time is to use the arguments:

[-p <time_in_seconds', --preparationTime=<time_in_seconds>]
                Max preparation time before tests in seconds. Default is 300.
                
and         

[-t <time_in_seconds>,--time=<time_in_seconds>]
	Execution time per test in seconds. Default is 30.
               

See diskBench -h for more advanced usage



	