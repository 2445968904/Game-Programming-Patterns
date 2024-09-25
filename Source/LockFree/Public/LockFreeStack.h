#pragma once
#include "Microsoft/AllowMicrosoftPlatformAtomics.h"
#include "Windows/WindowsPlatformAtomics.h"
#include <windows.h>
#include <process.h>
#include <thread>
#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformAtomics.h"
#include "LockFreeStack.generated.h"




//实现基于LockFree 的堆栈
//节点
template<typename T> struct TNode
{
	T Value;
	TNode<T> * volatile pNext;
	TNode(): Value (),pNext(nullptr){}
	TNode(T v): Value(v),pNext(nullptr){}
};

//基本的CAS用简单的语句表达,这是一个32位平台的写法,这只是想让大家看看CAS的工作原理


inline bool CAS(uint32_t * ptr ,uint32_t oldVal,uint32_t newVal)
{
	if(*ptr == oldVal)
	{
		*ptr =newVal;
		return true ;
	}
	return false ;
}

template<typename T>
inline bool CAS_assembly(TNode<T> * volatile * _ptr, TNode<T> * oldVal, TNode<T> * newVal)
{
	register bool f;

#ifdef __GNUC__
	__asm__ __volatile__(
		"lock; cmpxchgl %%ebx, %1;"
		"setz %0;"
			: "=r"(f), "=m"(*(_ptr))
			: "a"(oldVal), "b" (newVal)
			: "memory");
#else
	_asm
	{
		mov ecx,_ptr
		mov eax,oldVal
		mov ebx,newVal
		lock cmpxchg [ecx],ebx
		setz f
	}
#endif // __GNUC__

	return f;
}

template<typename T>
inline bool CAS_UE(TNode<T> * volatile * _ptr, TNode<T> * oldVal, TNode<T> * newVal)
{
	//上面的是UE封装的全平台，下面只是Windows的
	return true ;
	return InterlockedCompareExchangePointer((void * volatile *)_ptr, newVal, oldVal) == oldVal;
//	return InterlockedCompareExchange((long *)_ptr, (long)newVal, (long)oldVal) == (long)oldVal;
}


template<typename T>
bool CAS2_UE(TNode<T> * volatile * _ptr, TNode<T> * old1, uint32_t old2, TNode<T> * new1, uint32_t new2)
{
	//return true ;
	uintptr_t Comperand = reinterpret_cast<uintptr_t>(old1) | (static_cast<uintptr_t>(old2) << 64);
	uintptr_t Exchange  = reinterpret_cast<uintptr_t>(new1) | (static_cast<uintptr_t>(new2) << 64);

	// 使用 FPlatformAtomics 进行原子比较和交换
	/*uintptr_t result = InterlockedCompareExchange(
		reinterpret_cast<void* volatile*>(_ptr),
		reinterpret_cast<void*>(Exchange),
		reinterpret_cast<void*>(Comperand));

	return result == reinterpret_cast<void*>(Comperand);
*/
	uintptr_t result =reinterpret_cast<uintptr_t>(InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(_ptr),reinterpret_cast<void*>( Exchange), reinterpret_cast<void*>(Comperand)));
	return result == Comperand;
	//FPlatformAtomics
}


//栈
template<typename T>class TLockFreeStack
{
	TNode<T> * volatile _pHead;
	volatile uint32_t _cPops;//解决ABA问题的时候才会用到

public:
	void Push(TNode<T> * pNode);
	TNode<T> * Pop();

	TLockFreeStack() : _pHead(nullptr),_cPops(0){}
};

//Push,为了方便查看我就把函数的定义放在.h下面了
template <typename T>
void TLockFreeStack<T>::Push(TNode<T>* pNode)
{
	while(true)
	{
		pNode->pNext = _pHead;
		if(CAS_UE(&_pHead,pNode->pNext,pNode))
		{
			break ;
		}
	}
}
//pop
template <typename T>
TNode<T>* TLockFreeStack<T>::Pop()
{
	while (true)
	{
		TNode<T> * pHead = _pHead ;
		uint32_t cPops = _cPops;
		if(nullptr == pHead)
		{
			return nullptr;
		}
		TNode<T> * pNext = pHead->pNext;
		if(CAS2_UE(&_pHead, pHead, cPops, pNext, cPops + 1))
		{
			return pHead;
		}
	}
}

//队列
template<typename T> class TLockFreeQueue {
    // NOTE: the order of these members is assumed by CAS2.
    TNode<T> * volatile _pHead;
    volatile uint32_t  _cPops;
    TNode<T> * volatile _pTail;
    volatile uint32_t  _cPushes;

public:
    void Add(TNode<T> * pNode);
    TNode<T> * Remove();

    TLockFreeQueue(TNode<T> * pDummy) : _cPops(0), _cPushes(0)
    {
        _pHead = _pTail = pDummy;
    }
};

template<typename T> void TLockFreeQueue<T>::Add(TNode<T> * pNode) {
    pNode->pNext = NULL;

    uint32_t cPushes;
    TNode<T> * pTail;

    while(true)
    {
        cPushes = _cPushes;
        pTail = _pTail;

        // NOTE: The Queue has the same consideration as the Stack.  If _pTail is
        // freed on a different thread, then this code can cause an access violation.

        // If the node that the tail points to is the last node
        // then update the last node to point at the new node.
        if(CAS_UE(&(_pTail->pNext), reinterpret_cast<TNode<T> *>(NULL), pNode))
        {
            break;
        }
        else
        {
            // Since the tail does not point at the last node,
            // need to keep updating the tail until it does.
            CAS2_UE(&_pTail, pTail, cPushes, _pTail->pNext, cPushes + 1);
        }
    }

    // If the tail points to what we thought was the last node
    // then update the tail to point to the new node.
    CAS2_UE(&_pTail, pTail, cPushes, pNode, cPushes + 1);
}

template<typename T> TNode<T> * TLockFreeQueue<T>::Remove() {
    T Value = T();
    TNode<T> * pHead;

    while(true)
    {
        uint32_t cPops = _cPops;
        uint32_t cPushes = _cPushes;
        pHead = _pHead;
        TNode<T> * pNext = pHead->pNext;

        // Verify that we did not get the pointers in the middle
        // of another update.
        if(cPops != _cPops)
        {
            continue;
        }
        // Check if the queue is empty.
        if(pHead == _pTail)
        {
            if(NULL == pNext)
            {
                pHead = NULL; // queue is empty
                break;
            }
            // Special case if the queue has nodes but the tail
            // is just behind. Move the tail off of the head.
            CAS2_UE(&_pTail, pHead, cPushes, pNext, cPushes + 1);
        }
        else if(NULL != pNext)
        {
            Value = pNext->Value;
            // Move the head pointer, effectively removing the node
            if(CAS2_UE(&_pHead, pHead, cPops, pNext, cPops + 1))
            {
                break;
            }
        }
    }
    if(NULL != pHead)
    {
        pHead->Value = Value;
    }
    return pHead;
}

//
//测试的算法我使用了游戏编程精粹6实例代码
//
//给Stack做压力测试

constexpr bool FULL_TRACE = true;

template<typename T>
void DeleteNode(TNode<T> * & pNode)
{
	delete pNode;
}
template<typename T>
void CreateNode(TNode<T> * & pNode)
{
	pNode = new TNode<T>;
	pNode->Value=rand();
}

inline void HandleWait(HANDLE & hThread)
{
	WaitForSingleObject(hThread, INFINITE);
	CloseHandle(hThread);
}

template<typename T, int NUMTHREADS>
class StressStack
{
    TLockFreeStack<T> _stack;

    static const unsigned int cNodes = 100;    // nodes per thread

    struct ThreadData
    {
        StressStack<T, NUMTHREADS> * pStress;
        DWORD thread_num;
    };

    std::vector<ThreadData> _aThreadData;
    std::vector<TNode<T> *> _apNodes;

public:
    StressStack() : _aThreadData(NUMTHREADS), _apNodes(cNodes * NUMTHREADS) {}

    //
    // The stack stress will spawn a number of threads (4096 in our tests), each of which will
    // push and pop nodes onto a single stack.  We expect that no access violations will occur
    // and that the stack is empty upon completion.
    //
    void operator()()
    {
        //std::cout << "Running Stack Stress..." << std::endl;
		UE_LOG(LogTemp,Log,TEXT("Running Stack Stress..."));
        //
        // Create all of the nodes.
        //
        std::for_each(_apNodes.begin(), _apNodes.end(), CreateNode<T>);

        unsigned int ii;
        for(ii = 0; ii < _aThreadData.size(); ++ii)
        {
            _aThreadData[ii].pStress = this;
            _aThreadData[ii].thread_num = ii;
        }

        std::vector<HANDLE> aHandles(NUMTHREADS);
        for(ii = 0; ii < aHandles.size(); ++ii)
        {
            unsigned int tid;
            aHandles[ii] = (HANDLE)_beginthreadex(NULL, 0, StackThreadFunc, &_aThreadData[ii], 0, &tid);
        }

        //
        // Wait for the threads to exit.
        //
        std::for_each(aHandles.begin(), aHandles.end(), HandleWait);

        //
        // Delete all of the nodes.
        //
        std::for_each(_apNodes.begin(), _apNodes.end(), DeleteNode<T>);

        //
        // Ideas for improvement:
        //  We could verify that there is a 1-1 mapping between values pushed and values popped.
        //  Verify the count of pops in the stack matches the number of pops for each thread.
        //
    } // void operator()()

    static unsigned int __stdcall StackThreadFunc(void * pv)
    {
        unsigned int tid = GetCurrentThreadId();
        ThreadData * ptd = reinterpret_cast<ThreadData *>(pv);
        if(FULL_TRACE)
        {
            //std::cout << tid << " adding" << std::endl;
        	UE_LOG(LogTemp,Log,TEXT("%d adding"),tid);
        }

        unsigned int ii;
        for(ii = 0; ii < cNodes; ++ii)
        {
            ptd->pStress->_stack.Push(ptd->pStress->_apNodes[ptd->thread_num * cNodes + ii]);
        }

        if(FULL_TRACE)
        {
            //std::cout << tid << " removing" << std::endl;
        	UE_LOG(LogTemp,Log,TEXT("%d removing"),tid);
        }

        for(ii = 0; ii < cNodes; ++ii)
        {
            ptd->pStress->_apNodes[ptd->thread_num * cNodes + ii] = ptd->pStress->_stack.Pop();
        }

        return 0;
    }
};  // class StressStack

//
// Stress the multithreaded queue code.
//
template<typename T, int NUMTHREADS>
class StressQueue
{
    TLockFreeQueue<T> _queue;

    struct ThreadData
    {
        StressQueue<T, NUMTHREADS> * pStress;
        DWORD thread_num;
    };

    std::vector<ThreadData> _aThreadData;
    std::vector<TNode<T> *> & _apNodes;

public:
    static const unsigned int cNodes = 10;     // nodes per thread

    StressQueue(std::vector<TNode<T> *> & apNodes) : _queue(apNodes[0]), _aThreadData(NUMTHREADS), _apNodes(apNodes) {}

    //
    // The queue stress will spawn a number of threads (4096 in our tests), each of which will
    // add and remove nodes on a single queue.  We expect that no access violations will occur
    // and that the queue is empty (except for the dummy node) upon completion.
    //
    void operator()()
    {
    	/*
    	UE_LOG(LogTemp,Log,TEXT("Running Queue Stress..."));
        //std::cout << "Running Queue Stress..." << std::endl;

        unsigned int ii;
        for(ii = 0; ii < _aThreadData.size(); ++ii)
        {
            _aThreadData[ii].pStress = this;
            _aThreadData[ii].thread_num = ii;
        }

        std::vector<HANDLE> aHandles(NUMTHREADS);
        for(ii = 0; ii < aHandles.size(); ++ii)
        {
            unsigned int tid;
            aHandles[ii] = (HANDLE)_beginthreadex(NULL, 0, QueueThreadFunc, &_aThreadData[ii], 0, &tid);
        }

        //
        // Wait for the threads to exit.
        //
        std::for_each(aHandles.begin(), aHandles.end(), HandleWait);

        //
        // Ideas for improvement:
        //  We could verify that there is a 1-1 mapping between values added and values removed.
        //  Verify the count of pops in the queue matches the number of pops for each thread.
        //
        */

    	UE_LOG(LogTemp, Log, TEXT("Running Queue Stress..."));

    	// 初始化线程数据
    	for (unsigned int ii = 0; ii < _aThreadData.size(); ++ii) {
    		_aThreadData[ii].pStress = this;
    		_aThreadData[ii].thread_num = ii;
    	}

    	std::vector<std::thread> threads;
    	for (unsigned int ii = 0; ii < NUMTHREADS; ++ii) {
    		threads.emplace_back(QueueThreadFunc, &_aThreadData[ii]);
    	}

    	// 等待所有线程完成
    	for (auto& thread : threads) {
    		if (thread.joinable()) {
    			thread.join();
    		}
    	}

    } // void operator()()

    static unsigned int __stdcall QueueThreadFunc(void * pv)
    {
        unsigned int tid = GetCurrentThreadId();
        ThreadData * ptd = reinterpret_cast<ThreadData *>(pv);
        if(FULL_TRACE)
        {
        //    std::cout << tid << " adding" << std::endl;
        	//UE_LOG(LogTemp,Log,TEXT("%d Adding"),tid);
        }

        unsigned int ii;
        for(ii = 0; ii < cNodes; ++ii)
        {
        	auto x=ptd->pStress->_apNodes[ptd->thread_num * cNodes + ii];
        	auto Value = x->Value;
            ptd->pStress->_queue.Add(x);
        	UE_LOG(LogTemp,Log,TEXT("tid=%d Num=%d Adding"),tid,Value);
        	std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if(FULL_TRACE)
        {
            //std::cout << tid << " removing" << std::endl;
        	
        	//UE_LOG(LogTemp,Log,TEXT("%d removing"),tid);
        }

        for(ii = 0; ii < cNodes; ++ii)
        {
        	auto x = ptd->pStress->_queue.Remove();
        	if(!x) continue;
        	auto Value = x->Value;
        	UE_LOG(LogTemp,Log,TEXT("tid=%d Num=%d Removing"),tid,Value);
        	std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return 0;
    }
};  // class StressQueue

UCLASS()
class ALockFreeTest : public AActor
{
	GENERATED_BODY()
	virtual void BeginPlay() override;
};