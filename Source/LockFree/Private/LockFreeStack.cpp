#include "LockFreeStack.h"



void ALockFreeTest::BeginPlay()
{
	
	Super::BeginPlay();
	//StressStack<int, 128>()();

	std::vector<TNode<int> *> apNodes(StressQueue<int, 10>::cNodes * 10 + 1);   
	std::for_each(apNodes.begin(), apNodes.end(),CreateNode<int>);
	StressQueue<int, 2> theQueue(apNodes);
	theQueue();

	std::for_each(apNodes.begin(), apNodes.end(), DeleteNode<int>);
}
