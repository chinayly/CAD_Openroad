#include <cmath>
#include <cstdio>

#include "opt.h"

using namespace std;

class QudraticFunction : DifferentiableFunction
{
public:
    QudraticFunction(double xx, double xy, double yy, double x, double y, double c):xx(xx),xy(xy),yy(yy),x(x),y(y),c(c){}
    void initialGuess(double vx,double vy){
        this->vx = vx;
        this->vy = vy;
    }
    vector<double*> getParams();
    vector<double> getGradient();
    double eval();
private:
    //function
    double xx,xy,yy,x,y,c;
    // params
    double vx,vy; 
};

vector<double*> QudraticFunction::getParams()
{
    return vector{&vx,&vy};
}

vector<double> QudraticFunction::getGradient()
{
    double dx = 2*xx*vx + xy*vy + x;
    double dy = xy*vx + 2*yy*vy + y;
    return vector{-dx,-dy};
}

double QudraticFunction::eval()
{
    return xx*vx*vx + xy*vx*vy + yy*vy*vy + x*vx + y*vy + c;
}

#define EPS 1e-5

int main(int argc, char* argv[])
{
    QudraticFunction f = QudraticFunction(1,-1,3,2,-9,1);
    f.initialGuess(-1,-2);
    NesterovOptimizer opt = NesterovOptimizer((DifferentiableFunction*)&f);
    opt.initialize();
    for(size_t i = 0; i < 20; i++)
    {
        double Val = f.eval();
        printf("val = %f\n",Val); 
        opt.step();
    }
    auto v = f.getParams();
    printf("x = %f, y = %f\n",*v[0],*v[1]);
}