#include <fft.h>
#include <random>
#include <vector>
#include <iostream>

using namespace std;

// vec_2d random_vec_2d(int cntx, int cnty)
// {
//     int seed = 42;
//     mt19937 rng(seed);
//     uniform_real_distribution<double> dist(0,10);
//     vec_2d rand_vec;
//     for(int i = 0; i < cnty; i++)
//     {
//         vector<double> tmp;
//         tmp.resize(cntx);
//         for (int j = 0; j < cntx; j++)
//         {
//             tmp[j] = dist(rng);
//         }
//         rand_vec.emplace_back(std::move(tmp));
//     }
//     return rand_vec;
// }

// void print_vec_2d(const vec_2d& vec)
// {
//     for (auto v: vec)
//     {
//         for(auto e:v)
//         {
//             cout << e << ", ";
//         }
//         cout << endl;
//     }
// }

void set_random_density_2d(replace::FFT_2D& f, int cntx, int cnty)
{
    int seed = 42;
    mt19937 rng(seed);
    uniform_real_distribution<double> dist(0,10);
    for(int i = 0; i < cntx; i++)
    {
        for (int j = 0; j < cnty; j++)
        {
            f.updateDensity(i,j,dist(rng));
        }
    }
}

void set_random_density_3d(replace::FFT_3D& f, int cntx, int cnty, int cntz)
{
    int seed = 42;
    mt19937 rng(seed);
    uniform_real_distribution<double> dist(0,10);
    for(int i = 0; i < cntx; i++)
    {
        for (int j = 0; j < cnty; j++)
        {
            for (int k = 0; k < cntz; k++)
            {
                f.updateDensity(i,j,k,dist(rng));
            }
        }
    }
}

int main(){
    replace::FFT_3D fft(2,2,1,0.1,0.1,0.1);
    set_random_density_3d(fft,2,2,1);
    fft.doFFT();
    return 0;
}