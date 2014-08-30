#include "le_win.h"

static unsigned __stdcall thread_proc(void* arg) {
	thread_params* proxy = (thread_params* )arg;
	proxy->func(proxy->arg);

	free(proxy);

	_endthreadex(0);

	return 0;
}

int pthread_create(pthread_t* thread, const void* unused, void* (*start_routine)(void* ), void* arg) {
	HANDLE h;
	thread_params* params = (thread_params *)malloc(sizeof(thread_params));

	params->func = start_routine;
	params->arg  = arg;

	h = (HANDLE)_beginthreadex(NULL, 0, thread_proc, params, 0, thread);

	if( !h ) {
		free(params);
		return -1;
	}

	CloseHandle(h);

	return 0;
}

int pthread_join(pthread_t thread, void** unused) {
	HANDLE h = OpenThread(SYNCHRONIZE, FALSE, thread);

	if( WaitForSingleObject(h, INFINITE) ) {
		return -1;
	}

	CloseHandle(h);
	
	return 0;
}

