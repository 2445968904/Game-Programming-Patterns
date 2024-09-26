#pragma once
//#include "Microsoft/AllowMicrosoftPlatformAtomics.h"
#include "Windows/WindowsPlatformAtomics.h"
//#include <windows.h>
#include <process.h>

#include <thread>
#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformAtomics.h"
#include "LockFreeStack.generated.h"




//ʵ�ֻ���LockFree �Ķ�ջ
//�ڵ�
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
//������CAS�ü򵥵������,����һ��32λƽ̨��д��,��ֻ�����ô�ҿ���CAS�Ĺ���ԭ��


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
	//�������UE��װ��ȫƽ̨������ֻ��Windows��
	//return true ;
	
	return FPlatformAtomics::InterlockedCompareExchangePointer((void * volatile *)_ptr, newVal, oldVal) ==oldVal;
//	return InterlockedCompareExchange((long *)_ptr, (long)newVal, (long)oldVal) == (long)oldVal;
}

inline bool CAS2(uint32_t * ptr ,uint32_t oldVal,uint32_t OldVersion ,uint32_t newVal, uint32_t& NewVersion)
{
	if(*ptr == oldVal && OldVersion == NewVersion)
	{
		*ptr =newVal;
		NewVersion++;//�����NewVersionֻ����˵�����ֵ��Ҫ�������޸ģ��������Ǵ���������������͵�����
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

//ջ
template<typename T>class TLockFreeStack
{
	TNode<T> * volatile _pHead;
	volatile uint32_t _cPops;//���ABA�����ʱ��Ż��õ�

public:
	void Push(TNode<T> * pNode);
	TNode<T> * Pop();

	TLockFreeStack() : _pHead(nullptr),_cPops(0){}
};

//Push,Ϊ�˷���鿴�ҾͰѺ����Ķ������.h������
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

//����
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
    //TNode<T> cHead;
    TNode<T> * pTail=nullptr;
	int LoopNumber=0;
	//if(pNode == _pTail) UE_LOG(LogTemp,Log,TEXT("PNodeIs_PTail"));
    while(true)
    {
    	LoopNumber ++;
    	if(LoopNumber>5) break ;
      //  cHead = *_pHead;
        pTail = _pTail;
		//pTail->pNext = pNode;
        // NOTE: The Queue has the same consideration as the Stack.  If _pTail is
        // freed on a different thread, then this code can cause an access violation.

        // If the node that the tail points to is the last node
        // then update the last node to point at the new node.
        if(CAS_UE(&(_pTail->pNext), reinterpret_cast<TNode<T> *>(NULL), pNode))
        {
        	if(_pTail->pNext==_pTail)
        	UE_LOG(LogTemp,Log,TEXT("AddNext"));
        	
            break;
        }
        else
        {
            // Since the tail does not point at the last node,
            // need to keep updating the tail until it does.
        	UE_LOG(LogTemp,Log,TEXT("Not Add "));
            CAS2_UE(&_pTail, pTail ,_pTail->pNext);
        }
    }

    // If the tail points to what we thought was the last node
    // then update the tail to point to the new node.
    CAS2_UE(&_pTail, pTail, pNode);
}

template<typename T> TNode<T> * TLockFreeQueue<T>::Remove() {
    //T Value = T();
    TNode<T> * pHead;
    while(true)
    {
        pHead = _pHead;
        TNode<T> * pNext = pHead->pNext;

    	if(pNext==_pHead)UE_LOG(LogTemp,Log,TEXT("NO"));
        // Verify that we did not get the pointers in the middle
        // of another update.
        if(_pHead->Version != pHead->Version)
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
            CAS2_UE(&_pTail, pHead,pNext);
        }
        else if(NULL != pNext)
        {
            //Value = pNext->Value;
            // Move the head pointer, effectively removing the node
            if(CAS2_UE(&_pHead, pHead, pNext))
            {
            	//UE_LOG(LogTemp,Log,TEXT("YES"));
                break;
            }
        	else UE_LOG(LogTemp,Log,TEXT("SameBut"));
        }
    }
   
    return pHead;
}

//
//���Ե��㷨��ʹ������Ϸ��̾���6ʵ������
//
//��Stack��ѹ������

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
		Thread->WaitForCompletion(); // �ȴ��߳����
		delete Thread; // �ͷ��̶߳���
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

    	// ��ʼ���߳�����
    	for (unsigned int ii = 0; ii < _aThreadData.size(); ++ii) {
    		_aThreadData[ii].pStress = this;
    		_aThreadData[ii].thread_num = ii;
    	}

    	std::vector<std::thread> threads;
    	for (unsigned int ii = 0; ii < NUMTHREADS; ++ii) {
    		threads.emplace_back(QueueThreadFunc, &_aThreadData[ii]);
    	}

    	// �ȴ������߳����
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
        if(FULL_TRACE)
        {
        //    std::cout << tid << " adding" << std::endl;
        	//UE_LOG(LogTemp,Log,TEXT("%d Adding"),tid);
        }

        unsigned int ii;
        for(ii = 1; ii < cNodes; ++ii)
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