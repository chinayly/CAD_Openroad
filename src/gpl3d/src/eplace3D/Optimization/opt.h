#ifndef OPT_HPP
#define OPT_HPP

#include <vector>
#include <cstddef>
#include <functional>

typedef double Prec;

class DifferentiableFunction
{
public:
    virtual std::vector<Prec*> getParams() = 0;
    // not negative gradient
    virtual std::vector<Prec> getGradient() = 0;
};

class FirstOrderOptimizer
{
public:
    virtual void step()=0;
    virtual void initialize()=0;
protected:
    std::vector<Prec*> parameters;
};

class NesterovIter
{
public:
    void resize(size_t length);
    std::vector<Prec> majorSolution;
    std::vector<Prec> referenceSolution;
    std::vector<Prec> gradient;
};

void idProcess(std::vector<Prec*>& v);

class NesterovOptimizer : FirstOrderOptimizer
{
public:
    NesterovOptimizer(DifferentiableFunction* function,
        std::function<void(std::vector<Prec*>&)> postProcess = idProcess);
    void step();
    void initialize();
    std::size_t iterCount;
private:
    void stepVanilla();
    void stepBB();
    DifferentiableFunction* optFunction;
    std::function<void(std::vector<Prec*>&)> postProcess;
    Prec nsOptParam;
    NesterovIter curIter, lastIter;
};

#endif