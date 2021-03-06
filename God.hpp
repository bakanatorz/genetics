/*
 *  God.hpp
 *  Copyright (C) 2012 Eric Bakan
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GOD_HPP
#define GOD_HPP

#include "Algo.hpp"
#include "Heap.hpp"
#include "Processor.hpp"

#include <algorithm>
#include <math.h>
#include <pthread.h>
#include <sstream>
#include <vector>

/**
 * Game Master / God Class
 * Oversees the "natural selection" of algorithms from generation to generation
 * Different exit conditions available by passing a functor to update()
 * Online descriptive statistics derived from: ftp://reports.stanford.edu/pub/cstr/reports/cs/tr/79/773/CS-TR-79-773.pdf
 **/

struct AlgoScore
{
    Algo* algo;
    Processor::Score score;
};

template<typename H>
struct threadData
{
    const std::vector<Algo*>* population;
    unsigned int start;
    unsigned int stop;
    unsigned int successorSize;
    const Processor*  processor;
    pthread_mutex_t* mutex;
    Heap<AlgoScore, H>* scores;
    double* popM;
    double* popBar;
    unsigned int* popN;
};

template<typename H> void* Process(void* param)
{
    threadData<H>* td = static_cast<threadData<H>*>(param);
    Heap<AlgoScore, H> scores(td->successorSize, td->successorSize);
    double xM = 0.0, xBar = 0.0;
    unsigned int xN = td->stop - td->start;
    double *popM = td->popM, *popBar = td->popBar;
    unsigned int* popN = td->popN;
    for(unsigned int i = td->start; i < td->stop; i++)
    {
        Algo* algo = td->population->at(i);
        AlgoScore as;
        as.algo = algo;
        as.score = td->processor->process(algo);
        scores.Insert(as);
        double delta = as.score.score - xBar;
        xBar += delta / (i - td->start + 1);
        xM += delta * (as.score.score - xBar);
    }

    pthread_mutex_lock(td->mutex);
    if (*popN == 0)
    {
        *popM = xM;
        *popBar = xBar;
        *popN = xN;
    }
    else
    {
        double popM_ = *popM, popBar_ = *popBar;
        unsigned int popN_ = *popN;
        double delta = xBar - popBar_;
        double n = xN + popN_;
        double bar = (xN * xBar + popN_ * popBar_) / n;
        double m = xM + popM_ + delta * delta * xN * popN_ / n;
        *popM = m;
        *popBar = bar;
        *popN = n;
    }
    for(unsigned int i = 0; i < td->successorSize; i++)
    {
        td->scores->Insert(scores.Pop());
    }
    pthread_mutex_unlock(td->mutex);
    return 0;
}

class God
{
    public:

        struct greedyComplete
        {
            bool operator() (const std::vector<AlgoScore>& successors, unsigned int stepNum)
            {
                for (unsigned int j = 0; j < successors.size(); j++)
                {
                    const AlgoScore& as = successors[j];
                    if (as.score.success)
                    {
                        return true;
                    }
                }
                return false;
            }
        };

        struct patientComplete
        {
            bool operator() (const std::vector<AlgoScore>& successors, unsigned int stepNum)
            {
                return false; // Let the program complete with the most optimized value after the maximum number of iterations
            }
        };

        struct algoScoreSort
        {
            bool operator() (const AlgoScore& lhs, const AlgoScore& rhs)
            {
                Processor::Score l = lhs.score;
                Processor::Score r = rhs.score;
                if (l.success == r.success)
                {
                    return l.score < r.score;
                }
                return r.success;
            }
        };


        struct minScoreHeap
        {
            short operator() (const AlgoScore& lhs, const AlgoScore& rhs)
            {
                Processor::Score l = lhs.score;
                Processor::Score r = rhs.score;

                if (!l.success && r.success)
                {
                    return 1;
                }
                if (l.success && !r.success)
                {
                    return -1;
                }
                if (l.score < r.score)
                {
                    return -1;
                }
            
                if (l.score > r.score)
                {
                    return 1;
                }
            
                return 0;
            }
        };

        struct maxScoreHeap
        {
            short operator() (const AlgoScore& lhs, const AlgoScore& rhs)
            {
                Processor::Score l = lhs.score;
                Processor::Score r = rhs.score;

                if (!l.success && r.success)
                {
                    return 1;
                }
                if (l.success && !r.success)
                {
                    return -1;
                }
                if (l.score < r.score)
                {
                    return 1;
                }
            
                if (l.score > r.score)
                {
                    return -1;
                }
            
                return 0;
            }
        };

        God(const Processor& processor, const std::vector<Algo*>& seeds, unsigned int populationSize, unsigned int successorSize, unsigned int minThreadWorkloadSize, unsigned int maxNumThreads, unsigned int numCycles)
            : m_processor(processor)
            , m_seeds(seeds)
            , m_populationSize(populationSize)
            , m_successorSize(successorSize)
            , m_minThreadWorkloadSize(minThreadWorkloadSize)
            , m_maxNumThreads(maxNumThreads)
            , m_numCycles(numCycles)
        {
        }


        template<typename H, typename C> AlgoScore simulate()
        {
            std::vector<Algo*> population(m_populationSize);
            Heap<AlgoScore, H> scores(m_successorSize, m_successorSize);
            std::vector<AlgoScore> algoscores(m_successorSize);
            unsigned int numThreads = m_populationSize / m_minThreadWorkloadSize;
            if (numThreads > m_maxNumThreads)
            {
                numThreads = m_maxNumThreads;
            }
            std::vector<pthread_t> threads(numThreads);
            std::vector<threadData<H> > threadDatas(numThreads);
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
            pthread_mutex_t mutex;
            pthread_mutex_init(&mutex, NULL);
            AlgoScore* best = NULL;
            double prevAvg = 0.0, prevBest = 0.0;
            for(unsigned int i = 1; i <= m_numCycles; i++)
            {
                double popM = 0.0, popBar = 0.0;
                unsigned int popN = 0;
                printf("Generation %d/%d\n",i,m_numCycles);
                if (i == 1)
                {
                    unsigned int numSeeds = m_seeds.size();
                    for(unsigned int j = 0; j < m_populationSize; j++)
                    {
                        population[j] = m_seeds[j%numSeeds]->gen();
                    }
                    for(unsigned int j = 0; j < m_seeds.size(); j++)
                    {
                        delete m_seeds[j];
                        m_seeds[j] = 0;
                    }
                }
                else
                {
                    std::vector<Algo*> newpop(m_populationSize);
                    newpop[0] = best->algo;
                    for(unsigned int j = 1; j < m_populationSize; j++)
                    {
                        AlgoScore as = algoscores[j%m_successorSize];
                        newpop[j] = as.algo->gen();
                    }
                    for(unsigned int j = 0; j < m_populationSize; j++)
                    {
                        if (population[j] != best->algo)
                        {
                            delete population[j];
                        }
                        population[j] = newpop[j];
                    }
                }

                scores.Flush();

                for(unsigned int j = 0; j < numThreads; j++)
                {
                    threadData<H> td = {&population, j * m_populationSize / numThreads, (j + 1) * m_populationSize / numThreads, m_successorSize, &m_processor, &mutex, &scores, &popM, &popBar, &popN};
                    if (j == numThreads-1)
                    {
                        td.stop = m_populationSize;
                    }
                    threadDatas[j] = td;
                    pthread_create(&threads[j], &attr, Process<H>, (void*) (&threadDatas[j]));
                }
                for(unsigned int j = 0; j < numThreads; j++)
                {
                    void* status;
                    pthread_join(threads[j], &status);
                }

                for(unsigned int j = 0; j < m_successorSize; j++)
                {
                    algoscores[j] = scores.Pop();
                }
                best = &(*max_element(algoscores.begin(), algoscores.end(), m_sorter));

                double sigma = sqrt(popM/m_populationSize);

                printf("Average performance of population %d:\n", m_populationSize);
                printf("mu: %f sigma: %f\n", popBar, sigma);
                printf("Best Algo:\n");
                printf("%s",best->algo->getSummary().c_str());
                printf("\n");
                printf("Success: %d Score: %f\n", best->score.success, best->score.score);
                printf("\n");
                printf("%% above avg: %f\n", -(best->score.score-popBar)/popBar*100.0);
                printf("Std above avg: %f\n", -(best->score.score-popBar)/sigma);
                printf("%% score change from prev: avg: %f best: %f\n", -(popBar - prevAvg) / prevAvg * 100.0, -(best->score.score - prevBest) / prevBest * 100.0);
                std::stringstream ss;
                ss << i << ".log";
                m_processor.process(best->algo, ss.str());
                printf("\n");

                prevBest = best->score.score;
                prevAvg = popBar;

                C complete;
                if (complete(algoscores, i))
                {
                    for(unsigned int j = 0; j < m_populationSize; j++)
                    {
                        if (population[j] != best->algo)
                        {
                            delete population[j];
                            population[j] = NULL;
                        }
                    }
                    return *best;
                }
            }

            AlgoScore& winner = *max_element(algoscores.begin(), algoscores.end(), m_sorter);
            for(unsigned int j = 0; j < m_populationSize; j++)
            {
                if (population[j] != winner.algo)
                {
                    delete population[j];
                    population[j] = NULL;
                }
            }
            return winner;
        }

    private:
        const Processor& m_processor;
        std::vector<Algo*> m_seeds;
        unsigned int m_populationSize;
        unsigned int m_successorSize;
        unsigned int m_minThreadWorkloadSize;
        unsigned int m_maxNumThreads;
        unsigned int m_numCycles;
        algoScoreSort m_sorter;
};

#endif // GOD_HPP
