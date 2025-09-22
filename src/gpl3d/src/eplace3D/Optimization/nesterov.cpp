#include <algorithm>

#include "global.h"
#include "opt.h"
#include "optutils.hpp"


#define MAX_ITERATION 1000
#define BKTRK_EPS 0.95

void idProcess(std::vector<Prec*>& v){}

void NesterovIter::resize(size_t length)
{
    majorSolution.resize(length);
    referenceSolution.resize(length);
    gradient.resize(length);
}

NesterovOptimizer::NesterovOptimizer(DifferentiableFunction* function, std::function<void(std::vector<Prec*>&)> postProcess)
: optFunction(function),postProcess(postProcess)
{
    parameters = function->getParams();
    initialize();
}

void NesterovOptimizer::initialize()
{
    nsOptParam = 1;
    iterCount = 0;
    curIter.resize(parameters.size());
    transform(parameters.begin(),parameters.end(),curIter.majorSolution.begin(),[](Prec* x){return *x;}); 
    printf("length: %d\n",parameters.size());
}

void NesterovOptimizer::step()
{
    if(gArg.CheckExist("bb"))
    {
        stepBB();
    }
    else
    {
        stepVanilla();
    }
    iterCount++;
}

void NesterovOptimizer::stepVanilla()
{
    transform(parameters.begin(),parameters.end(),curIter.referenceSolution.begin(),[](Prec* x){return *x;}); 
    curIter.gradient = optFunction->getGradient();

    //calculate lipschitz constant
    Prec stepSize = 1;
    if (iterCount > 0)
    {
        Prec lipshitzConstant = calc_lipschitz_constant(curIter.referenceSolution,lastIter.referenceSolution,curIter.gradient,lastIter.gradient);
        stepSize = 1 / lipshitzConstant;
    }
    printf("stepSize : %f\n", stepSize);

    Prec newNsOptParam = (1 + sqrt(4 * nsOptParam * nsOptParam + 1)) / 2; // ak+1

    //perform one step
    std::size_t length = parameters.size();
    NesterovIter newIter;
    newIter.resize(length);
    for(std::size_t idx = 0; idx < length; idx++)
    {
        Prec& newMajor = newIter.majorSolution[idx];
        Prec gradient = curIter.gradient[idx];
        Prec curMajor = curIter.majorSolution[idx];
        Prec curReference = curIter.referenceSolution[idx];

        newMajor = curIter.referenceSolution[idx] - gradient * stepSize;
        *parameters[idx] = newMajor + (newMajor - curMajor) * ((nsOptParam - 1) / newNsOptParam);
    }
    postProcess(parameters);
    lastIter = curIter;
    curIter = newIter;
    nsOptParam = newNsOptParam;
}

void NesterovOptimizer::stepBB()
{
    transform(parameters.begin(),parameters.end(),curIter.referenceSolution.begin(),[](Prec* x){return *x;}); 
    curIter.gradient = optFunction->getGradient();

    //calculate lipschitz constant
    Prec stepSize = 1;
    if (iterCount > 0)
    {
        Prec lipshitzConstant = calc_lipschitz_constant(curIter.referenceSolution,lastIter.referenceSolution,curIter.gradient,lastIter.gradient);
        stepSize = 1 / lipshitzConstant;
    }
    printf("stepSize : %f\n", stepSize);

    Prec newNsOptParam = (1 + sqrt(4 * nsOptParam * nsOptParam + 1)) / 2; // ak+1

    //perform one step
    std::size_t length = parameters.size();
    NesterovIter newIter;
    newIter.resize(length);
    for(std::size_t idx = 0; idx < length; idx++)
    {
        Prec& newMajor = newIter.majorSolution[idx];
        Prec gradient = curIter.gradient[idx];
        Prec curMajor = curIter.majorSolution[idx];
        Prec curReference = curIter.referenceSolution[idx];

        newMajor = curIter.referenceSolution[idx] - gradient * stepSize;
        *parameters[idx] = newMajor + (newMajor - curMajor) * ((nsOptParam - 1) / newNsOptParam);
    }
    postProcess(parameters);
    lastIter = curIter;
    curIter = newIter;
    nsOptParam = newNsOptParam;
}