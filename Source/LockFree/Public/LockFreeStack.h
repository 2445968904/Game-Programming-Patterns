#pragma once
//#include "Microsoft/AllowMicrosoftPlatformAtomics.h"
#include "Windows/WindowsPlatformAtomics.h"
//#include <windows.h>
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
	int32 Version;
	TNode(): Value (),pNext(nullptr),Version(){}
	TNode(T v): Value(v),pNext(nullptr),Version(){}
};

template<typename T>
struct TQueuePoint
{
	TNode<T> *Node;
	int32 Version;
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
	//return true ;
	
	return FPlatformAtomics::InterlockedCompareExchangePointer((void * volatile *)_ptr, newVal, oldVal) ==oldVal;
//	return InterlockedCompareExchange((long *)_ptr, (long)newVal, (long)oldVal) == (long)oldVal;
}

inline bool CAS2(uint32_t * ptr ,uint32_t oldVal,uint32_t OldVersion ,uint32_t newVal, uint32_t& NewVersion)
{
	if(*ptr == oldVal && OldVersion == NewVersion)
	{
		*ptr =newVal;
		NewVersion++;//这里叫NewVersion只是想说明这个值需要在这里修改，并且我们传进来的是这个整型的引用
		return true ;
	}
	return false ;
}

template<typename T>
bool CAS2_UE(TNode<T>* volatile* _ptr, TNode<T>* oldVal ,TNode<T>* newVal)
{
	newVal->Version = newVal->Version+1;
	return FPlatformAtomics::InterlockedCompareExchangePointer((void * volatile *)_ptr, newVal, oldVal) == oldVal;
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
    //TNode<T> * volatile _pHead;
    //volatile uint32_t  _cPops;
    //TNode<T> * volatile _pTail;
    //volatile uint32_t  _cPushes;

	TNode<T> * volatile _pHead;
	TNode<T> * volatile _pTail;

public:
    void Add(TNode<T> * pNode);
    TNode<T> * Remove();

   
	TLockFreeQueue(TNode<T>*pDunmy)
		
    {
		_pHead = _pTail = pDunmy;
    }
};

template<typename T> void TLockFreeQueue<T>::Add(TNode<T> * pNode) {
    pNode->pNext = NULL;
	if(!pNode) return ;
    TNode<T> * pTail=nullptr;
    while(true)
    {
        pTail = _pTail;
    	
    	//如果Tail的next指针为空，就将它设置为pNode
        if(CAS2_UE(&(_pTail->pNext), reinterpret_cast<TNode<T> *>(NULL), pNode))
        {
        	//UE_LOG(LogTemp,Log,TEXT("AddNext"));//也可以用cout
            break;
        }
        else
        {
            //更新Tail直到没有Next
        	//UE_LOG(LogTemp,Log,TEXT("Not Add "));
            CAS2_UE(&_pTail, pTail ,_pTail->pNext);
        }
    }

    //如果走了一圈我们的Tail还是以前那个Tail,我们就将它设置为Node
	//这个函数执行主要是上面那个Break，不用保证一定成功,因为上面的else可以在下次刷新
    CAS2_UE(&_pTail, pTail, pNode);
}

template<typename T> TNode<T> * TLockFreeQueue<T>::Remove() {
    
    TNode<T> * pHead=nullptr;
    while(true)
    {
        pHead = _pHead;
        TNode<T> * pNext = pHead->pNext;
    	
        // 检查队列是否为空
        if(pHead == _pTail)
        {
            if(NULL == pNext)
            {
                pHead = NULL; // pHead 置空
                break;
            }
            //如果还有元素，说明PTail改往后移动了
            CAS2_UE(&_pTail, pHead,pNext);
        }
        else if(NULL != pNext)
        
            if(CAS2_UE(&_pHead, pHead, pNext))
            {
            	//Head 移除成功
                break;
            }
        	
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

inline void HandleWait(FRunnableThread* Thread)
{
	if (Thread)
	{
		Thread->WaitForCompletion(); // 等待线程完成
		delete Thread; // 释放线程对象
	}
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
    static const unsigned int cNodes = 10;     // 每个线程会操作的节点量

    StressQueue(std::vector<TNode<T> *> & apNodes) : _queue(apNodes[0]), _aThreadData(NUMTHREADS), _apNodes(apNodes) {}

    void operator()()
    {
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
        unsigned int tid = Windows::GetCurrentThreadId();
        ThreadData * ptd = reinterpret_cast<ThreadData *>(pv);

        unsigned int ii;
        for(ii = 1; ii < cNodes; ++ii)
        {
        	auto x=ptd->pStress->_apNodes[ptd->thread_num * cNodes + ii];
        	auto Value = x->Value;
            ptd->pStress->_queue.Add(x);
        	UE_LOG(LogTemp,Log,TEXT("tid=%d Num=%d Adding"),tid,Value);
        	std::this_thread::sleep_for(std::chrono::milliseconds(10));
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