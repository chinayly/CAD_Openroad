#include <cstdio>

#include "placer.h"
#include "fft.h"

ElectricMesh2D::ElectricMesh2D(POS_2D ll, POS_2D ur, size_t dimX, size_t dimY)
:ll(ll),ur(ur),dimX(dimX),dimY(dimY)
{
    binWidth = (ur.x - ll.x) / dimX;
    binHeight = (ur.y - ll.y) / dimY;
    rho_fillers.resize(dimX);
    rho_modules.resize(dimX);
    for(auto& vecy:rho_fillers)
    {
        vecy.resize(dimY);
        std::fill(vecy.begin(),vecy.end(),0.0f);
    }
    for(auto& vecy:rho_modules)
    {
        vecy.resize(dimY);
        std::fill(vecy.begin(),vecy.end(),0.0f);
    }
    E.resize(dimX);
    for(auto& vecy:E)
    {
        vecy.resize(dimY);
        std::fill(vecy.begin(),vecy.end(),VECTOR_2D());
    }
}

void ElectricMesh2D::clearDensity()
{
    for(auto& vecy:rho_fillers)
    {
        std::fill(vecy.begin(),vecy.end(),0.0f);
    }
    for(auto& vecy:rho_modules)
    {
        std::fill(vecy.begin(),vecy.end(),0.0f);
    }
}

double getOverlapArea(const POS_2D &ll1, const POS_2D&ur1,
                        const POS_2D &ll2, const POS_2D&ur2)
{
    double dx = std::max(0.0, std::min(ur1.x, ur2.x) - std::max(ll1.x, ll2.x));
    double dy = std::max(0.0, std::min(ur1.y, ur2.y) - std::max(ll1.y, ll2.y));
    return dx * dy; 
}

void ElectricMesh2D::addModuleDensity(const CRect& rec, double scale)
{
    int binStartX = std::max((int)0, (int)std::floor((rec.ll.x - ll.x) / binWidth));
    int binEndX = std::min((int)dimX - 1, (int)std::floor((rec.ur.x - ll.x) / binWidth));

    int binStartY = std::max((int)0, (int)std::floor((rec.ll.y - ll.y) / binHeight));
    int binEndY = std::min((int)dimY - 1, (int)std::floor((rec.ur.y - ll.y) / binHeight));

    for (int i = binStartX; i <= binEndX; ++i)
    {
        for (int j = binStartY; j <= binEndY; ++j)
        {
            const POS_2D llBin = POS_2D{ll.x + i * binWidth, ll.y + j * binHeight};
            const POS_2D urBin = POS_2D{ll.x + (i + 1) * binWidth, ll.y + (j + 1) * binHeight};
            double area = getOverlapArea(llBin,urBin,rec.ll,rec.ur);
            rho_modules[i][j] += area * scale;
        }
    }
}

void ElectricMesh2D::addFillerDensity(const CRect& rec, double scale)
{
    int binStartX = std::max((int)0, (int)std::floor((rec.ll.x - ll.x) / binWidth));
    int binEndX = std::min((int)dimX - 1, (int)std::floor((rec.ur.x - ll.x) / binWidth));

    int binStartY = std::max((int)0, (int)std::floor((rec.ll.y - ll.y) / binHeight));
    int binEndY = std::min((int)dimY - 1, (int)std::floor((rec.ur.y - ll.y) / binHeight));

    for (int i = binStartX; i <= binEndX; ++i)
    {
        for (int j = binStartY; j <= binEndY; ++j)
        {
            const POS_2D llBin = POS_2D{ll.x + i * binWidth, ll.y + j * binHeight};
            const POS_2D urBin = POS_2D{ll.x + (i + 1) * binWidth, ll.y + (j + 1) * binHeight};
            double area = getOverlapArea(llBin,urBin,rec.ll,rec.ur);
            rho_fillers[i][j] += area * scale;
        }
    }
}

void ElectricMesh2D::calcElectricField()
{
    auto fft = replace::FFT_2D(dimX,dimY,binWidth,binHeight);
    double binArea = this->getBinArea();
    for(size_t i = 0; i < dimX; i++)
    {
        for(size_t j = 0; j < dimX; j++)
        {
            fft.updateDensity(i,j,(rho_fillers[i][j] + rho_modules[i][j]) / binArea); 
        }
    }
    fft.doFFT();
    for(size_t i = 0; i < dimX; i++)
    {
        for(size_t j = 0; j < dimX; j++)
        {
            VECTOR_2D& Eij = E[i][j];
            auto&& force = fft.getElectroForce(i,j);
            Eij.x = force.first;
            Eij.y = force.second;
        }
    }
}

double ElectricMesh2D::accumulateModuleDensity(function<double(double,double)> acc, double initial) const
{
    double result = initial;
    for(const auto& rhoy:rho_modules)
    {
        for(const auto& rhoxy:rhoy)
        {
            result = acc(rhoxy,result);
        }
    }
    return result;
}

VECTOR_2D ElectricMesh2D::getElectricForce(const CRect& rec) const
{
    int binStartX = std::max((int)0, (int)std::floor((rec.ll.x - ll.x) / binWidth));
    int binEndX = std::min((int)dimX - 1, (int)std::floor((rec.ur.x - ll.x) / binWidth));
    int binStartY = std::max((int)0, (int)std::floor((rec.ll.y - ll.y) / binHeight));
    int binEndY = std::min((int)dimY - 1, (int)std::floor((rec.ur.y - ll.y) / binHeight));
    VECTOR_2D force;
    for (int i = binStartX; i <= binEndX; ++i)
    {
        for (int j = binStartY; j <= binEndY; ++j)
        {
            const POS_2D llBin = POS_2D{ll.x + i * binWidth, ll.y + j * binHeight};
            const POS_2D urBin = POS_2D{ll.x + (i + 1) * binWidth, ll.y + (j + 1) * binHeight};
            double area = getOverlapArea(llBin,urBin,rec.ll,rec.ur);
            force += E[i][j] * area;
        }
    }
    return force;
}

void ElectricMesh2D::show() const
{
    cout << "Mesh ll:" << ll << ",ur:" << ur << endl;
    for(const auto& rhoy:rho_modules)
    {
        for(const auto& rhoxy:rhoy)
        {
            printf("%.4f\t",rhoxy);
        }
        cout << endl;
    }
    cout << endl;
}