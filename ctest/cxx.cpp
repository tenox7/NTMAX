// cxx.cpp -- C++: virtual functions, new/delete

class Shape {
public:
	virtual int area() = 0;
	virtual ~Shape() {}
};

class Square : public Shape {
	int side;
public:
	Square(int s) : side(s) {}
	int area() { return side * side; }
};

int test(int s)
{
	Shape *p = new Square(s);
	int a = p->area();

	delete p;
	return a;
}
