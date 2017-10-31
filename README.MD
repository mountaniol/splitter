SPLITTER
========

-Author: 	Sebastian Mountaniol
-License: 	GPL
-Name: 		Splitter


**Dscription**

Splitter: splits file(s) into chunks / joins origin file from sthe chunks.
Every chunk may have a different size.
Compatible with HJ-Split software (at least in case of splitting to chunks of the same size).
It supports large files as well.

**Usage** 

Split: splitter -s filename size1 size2 size3 ... sizeN [-r]
Join:  splitter -j filename.000

-r in split command means "repeat". It repeats the chain of sizes from 1 to N
(see examples below)

**Example of usage**

Example 1
-splitter -s /path/to/file.name 1024m
-Result: file /path/to/file.name will be split to /path/to/file.name.000 .001 and so on
-every chunk is 1024 megabytes

Example 2
-splitter -s /path/to/file.name 1024m 512m 128m
-Result: file /path/to/file.name will be split to: 

-/path/to/file.name.000 size 1024 Megabytes
-/path/to/file.name.001 size 512 Megabytes
-/path/to/file.name.002 size 128 Megabytes
-/path/to/file.name.003 ... /path/to/file.name.NNN, every file has size 128 Megabytes 

Example 3
-splitter -s /path/to/file.name 1024m 512m 128m -r
-Result: file /path/to/file.name will be split to: 

-/path/to/file.name.000 size 1024 Megabytes
-/path/to/file.name.001 size 512 Megabytes
-/path/to/file.name.002 size 128 Megabytes
-/path/to/file.name.003 size 1024 Megabytes
-/path/to/file.name.004 size 512 Megabytes
-/path/to/file.name.005 size 128 Megabytes
-...
-/path/to/file.name.(NNN -2)  size 1024 Megabytes
-/path/to/file.name.(NNN - 1) size 512 Megabytes
-/path/to/file.name.NNN       size 128 Megabytes

** Contact me**
-Feel free asking questions. Contact me through github:
-http://github.com/mountaniol
