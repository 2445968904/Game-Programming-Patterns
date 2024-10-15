#pragma once

class BSPTree
{
public:
	
};


class CBspNode  
{
public:
	CBspNode* Back();
	CBspNode* Front();
	CBspNode* Parent();
	
	list<Polygon> Polygons;
	Plane plane;
};

CBspNode* BuildBSP(list<Polygon> polygons ,CBspNode& Node)
{
	if(polygons.isEmpty())
		return nullptr;
}
