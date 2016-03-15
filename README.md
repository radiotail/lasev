## lasev - a lite asynchronous event lib

## OVERVIEW
	lasev is a lite asynchronous network library. It's idea derived form libuv, but much more cleanly, because lasev's function is very concise. lasev's API is very like libuv, so if you familiar libuv, you should easy to get started. 

## FEATURE
	asynchronous tcp sockets
	support iocp, epoll
	timer event
	thread queue

## BUILDS
Use premake4 to generate project files.
```
$ cd premake4
$ premake4 --file=xxx.lua action
```

example:
```
$ premake4 --file=test_lasev.lua gmake
```

[Premake Quick Start](http://industriousone.com/premake-quick-start).

## SUPPORTED PLATFORMS
Windows, Linux.

