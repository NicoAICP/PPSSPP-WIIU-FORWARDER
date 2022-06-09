#pragma once
#include <wiiu/types.h>
#include "time.h"
#include "wiiu/os/exception.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OS_THREAD_DEF_PRIO     0x10
#define OS_THREAD_SPECIFIC_MAX 0x10

typedef struct OSThread OSThread;

typedef int (*OSThreadEntryPointFn)(int argc, const char **argv);
typedef void (*OSThreadCleanupCallbackFn)(OSThread *thread, void *stack);
typedef void (*OSThreadDeallocatorFn)(OSThread *thread, void *stack);

enum OS_THREAD_STATE
{
   OS_THREAD_STATE_NONE             = 0,

   /*! Thread is ready to run */
   OS_THREAD_STATE_READY            = 1 << 0,

   /*! Thread is running */
   OS_THREAD_STATE_RUNNING          = 1 << 1,

   /*! Thread is waiting, i.e. on a mutex */
   OS_THREAD_STATE_WAITING          = 1 << 2,

   /*! Thread is about to terminate */
   OS_THREAD_STATE_MORIBUND         = 1 << 3,
};
typedef uint8_t OSThreadState;

enum OS_THREAD_REQUEST
{
   OS_THREAD_REQUEST_NONE           = 0,
   OS_THREAD_REQUEST_SUSPEND        = 1,
   OS_THREAD_REQUEST_CANCEL         = 2,
};
typedef uint32_t OSThreadRequest;

enum OS_THREAD_ATTRIB
{
   /*! Allow the thread to run on CPU0. */
   OS_THREAD_ATTRIB_AFFINITY_CPU0   = 1 << 0,

   /*! Allow the thread to run on CPU1. */
   OS_THREAD_ATTRIB_AFFINITY_CPU1   = 1 << 1,

   /*! Allow the thread to run on CPU2. */
   OS_THREAD_ATTRIB_AFFINITY_CPU2   = 1 << 2,

   /*! Allow the thread to run any CPU. */
   OS_THREAD_ATTRIB_AFFINITY_ANY    = ((1 << 0) | (1 << 1) | (1 << 2)),

   /*! Start the thread detached. */
   OS_THREAD_ATTRIB_DETACHED        = 1 << 3,

   /*! Enables tracking of stack usage. */
   OS_THREAD_ATTRIB_STACK_USAGE     = 1 << 5
};
typedef uint8_t OSThreadAttributes;


typedef struct OSMutex OSMutex;
typedef struct OSFastMutex OSFastMutex;

typedef struct OSMutexQueue
{
   OSMutex *head;
   OSMutex *tail;
   void *parent;
   uint32_t __unknown;
} OSMutexQueue;

typedef struct OSFastMutexQueue
{
   OSFastMutex *head;
   OSFastMutex *tail;
} OSFastMutexQueue;


typedef struct
{
   OSThread *prev;
   OSThread *next;
} OSThreadLink;

typedef struct
{
   OSThread *head;
   OSThread *tail;
   void *parent;
   uint32_t __unknown;
} OSThreadQueue;

typedef struct
{
   OSThread *head;
   OSThread *tail;
} OSThreadSimpleQueue;

#define OS_THREAD_TAG 0x74487244u
typedef struct OSThread
{
   OSContext context;

   /*! Should always be set to the value OS_THREAD_TAG. */
   uint32_t tag;

   /*! Bitfield of OS_THREAD_STATE */
   OSThreadState state;

   /*! Bitfield of OS_THREAD_ATTRIB */
   OSThreadAttributes attr;

   /*! Unique thread ID */
   uint16_t id;

   /*! Suspend count (increased by OSSuspendThread). */
   int32_t suspendCounter;

   /*! Actual priority of thread. */
   int32_t priority;

   /*! Base priority of thread, 0 is highest priority, 31 is lowest priority. */
   int32_t basePriority;

   /*! Exit value */
   int32_t exitValue;

   uint32_t unknown0[0x9];

   /*! Queue the thread is currently waiting on */
   OSThreadQueue *queue;

   /*! Link used for thread queue */
   OSThreadLink link;

   /*! Queue of threads waiting to join this thread */
   OSThreadQueue joinQueue;

   /*! Mutex this thread is waiting to lock */
   OSMutex *mutex;

   /*! Queue of mutexes this thread owns */
   OSMutexQueue mutexQueue;

   /*! Link for global active thread queue */
   OSThreadLink activeLink;

   /*! Stack start (top, highest address) */
   void *stackStart;

   /*! Stack end (bottom, lowest address) */
   void *stackEnd;

   /*! Thread entry point */
   OSThreadEntryPointFn entryPoint;

   uint32_t unknown1[0x77];

   /*! Thread specific values, accessed with OSSetThreadSpecific and OSGetThreadSpecific. */
   void* specific[OS_THREAD_SPECIFIC_MAX];

   uint32_t unknown2;

   /*! Thread name, accessed with OSSetThreadName and OSGetThreadName. */
   const char *name;

   uint32_t unknown3;

   /*! The stack pointer passed in OSCreateThread. */
   void *userStackPointer;

   /*! Called just before thread is terminated, set with OSSetThreadCleanupCallback */
   OSThreadCleanupCallbackFn cleanupCallback;

   /*! Called just after a thread is terminated, set with OSSetThreadDeallocator */
   OSThreadDeallocatorFn deallocator;

   /*! If TRUE then a thread can be cancelled or suspended, set with OSSetThreadCancelState */
   BOOL cancelState;

   /*! Current thread request, used for cancelleing and suspending the thread. */
   OSThreadRequest requestFlag;

   /*! Pending suspend request count */
   int32_t needSuspend;

   /*! Result of thread suspend */
   int32_t suspendResult;

   /*! Queue of threads waiting for a thread to be suspended. */
   OSThreadQueue suspendQueue;

   int32_t unknown4[0xF];
   OSExceptionCallbackFn dsiExCallback[3];
   OSExceptionCallbackFn isiExCallback[3];
   OSExceptionCallbackFn programExCallback[3];
   OSExceptionCallbackFn perfMonExCallback[3];
   int32_t unknown5;
   short tlsCnt;
   short unknown6;
   struct
   {
       void* address;
       int32_t tlsUnk;
   }* tls;
   int32_t unknown7 [0x5];
   OSExceptionCallbackFn alignExCallback[3];
   int32_t unknown8[0x5];
} OSThread;

void
OSCancelThread(OSThread *thread);
int32_t OSCheckActiveThreads();
int32_t OSCheckThreadStackUsage(OSThread *thread);
void OSClearThreadStackUsage(OSThread *thread);
void OSContinueThread(OSThread *thread);
BOOL OSCreateThread(OSThread *thread, OSThreadEntryPointFn entry, int32_t argc, char **argv,
                    void *stack, uint32_t stackSize, int32_t priority, OSThreadAttributes attributes);
void OSDetachThread(OSThread *thread);
void OSExitThread(int32_t result);
void OSGetActiveThreadLink(OSThread *thread, OSThreadLink *link);
OSThread *OSGetCurrentThread();
OSThread *OSGetDefaultThread(uint32_t coreID);
uint32_t OSGetStackPointer();
uint32_t OSGetThreadAffinity(OSThread *thread);
const char *OSGetThreadName(OSThread *thread);
int32_t OSGetThreadPriority(OSThread *thread);
void* OSGetThreadSpecific(uint32_t id);
BOOL OSIsThreadSuspended(OSThread *thread);
BOOL OSIsThreadTerminated(OSThread *thread);
BOOL OSJoinThread(OSThread *thread, int *threadResult);
int32_t OSResumeThread(OSThread *thread);
BOOL OSRunThread(OSThread *thread, OSThreadEntryPointFn entry, int argc, const char **argv);
BOOL OSSetThreadAffinity(OSThread *thread, uint32_t affinity);
BOOL OSSetThreadCancelState(BOOL state);
OSThreadCleanupCallbackFn OSSetThreadCleanupCallback(OSThread *thread,
      OSThreadCleanupCallbackFn callback);
OSThreadDeallocatorFn OSSetThreadDeallocator(OSThread *thread, OSThreadDeallocatorFn deallocator);
void OSSetThreadName(OSThread *thread, const char *name);
BOOL OSSetThreadPriority(OSThread *thread, int32_t priority);
BOOL OSSetThreadRunQuantum(OSThread *thread, uint32_t quantum);
void OSSetThreadSpecific(uint32_t id, const void* value);
BOOL OSSetThreadStackUsage(OSThread *thread);
void OSSleepThread(OSThreadQueue *queue);
void OSSleepTicks(OSTime ticks);
uint32_t OSSuspendThread(OSThread *thread);
void OSTestThreadCancel();
void OSWakeupThread(OSThreadQueue *queue);
void OSYieldThread();
void OSInitThreadQueue(OSThreadQueue *queue);
void OSInitThreadQueueEx(OSThreadQueue *queue, void *parent);

void PPCSetFpIEEEMode (void);
void PPCSetFpNonIEEEMode (void);

#ifdef __cplusplus
}
#endif
