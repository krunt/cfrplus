#include <stdint.h>
#include <iostream>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <assert.h>

using namespace std;

typedef unsigned long long ull;

const int MAX_CARDS = 3;

int getRandInt(int n)
{
   return rand() % n;
}

struct StrategyNode {
   bool locked = false;
   string cards;
   string actionLabels;
   vector<double> regrets;
   vector<double> strategy;
   vector<double> strategySum;
   vector<double> avgStrategy;
};

void initStrategy(StrategyNode &node, const string &cards, const string &actionLabels)
{
   int n = (int)actionLabels.size();
   assert(n > 0);
   node.locked = false;
   node.cards = cards;
   node.actionLabels = actionLabels;
   node.regrets.resize(n, 0);
   node.strategy.resize(n, 1.0 / n);
   node.strategySum.resize(n, 0);
   node.avgStrategy.resize(n, 1.0 / n);
}

void regretMatching(StrategyNode &node)
{
   if (node.locked) {
      return;
   }

   double sum = 0;
   for (int i = 0; i < node.regrets.size(); ++i) {
      node.strategy[i] = node.regrets[i] > 0 ? node.regrets[i] : 0;
      sum += node.strategy[i];
   }
   for (int i = 0; i < node.regrets.size(); ++i) {
      if (sum > 0) {
         node.strategy[i] /= sum;
      }
      else {
         node.strategy[i] = 1.0 / node.actionLabels.size();
      }
   }
}

void accumulateAvgStrategy(StrategyNode &node, double weight)
{
   for (int i = 0; i < node.regrets.size(); ++i) {
      node.strategySum[i] += weight * node.strategy[i];
   }
}

void regretMatchingAvgStrategy(StrategyNode &node)
{
   double sum = 0;
   for (int i = 0; i < node.regrets.size(); ++i) {
      sum += node.strategySum[i];
   }
   for (int i = 0; i < node.regrets.size(); ++i) {
      if (sum > 0) {
         node.avgStrategy[i] = node.strategySum[i] / sum;
      }
      else {
         node.avgStrategy[i] = 1.0 / node.actionLabels.size();
      }
   }
}

struct CfrState {
   int cards[2];
   ull cardMask[2];
   double betSize;
   double initialPotSize;
   map<string, StrategyNode> strat;
   map<string, std::vector<double> > lockedStrat;
};

void initState(CfrState &state)
{
   state.cards[0] = state.cards[1] = -1;
   state.cardMask[0] = state.cardMask[1] = (ull)-1;

   state.betSize = 1;
   state.initialPotSize = 1;

   state.strat.clear();
}

string tostring(int num) 
{
   char buf[16];
   sprintf(buf, "%d", num);
   return buf;
}

string constructInfosetKey(const string &hist, int card)
{
   return hist + "|" + tostring(card);
}

string getInfosetKey(const CfrState &state, int player, string hist)
{
   return constructInfosetKey(hist, state.cards[player]);
}

void lockNode(CfrState &state, const string &hist, int card, const std::vector<double> &strategy)
{
   auto key = constructInfosetKey(hist, card);
   state.lockedStrat[key] = strategy;
}

void chooseCards(CfrState &state)
{
   do {
      state.cards[0] = getRandInt(MAX_CARDS);
   } while (!(state.cardMask[0] & (1ull << state.cards[0])));

   do {
      state.cards[1] = getRandInt(MAX_CARDS);
   } while (!(state.cardMask[1] & (1ull << state.cards[1])) || state.cards[0] == state.cards[1]);
}

double showdown(CfrState &state, char action, int player)
{
   assert(state.cards[player] != state.cards[!player]);

   double blind = state.initialPotSize * 0.5;
   if (action == 'f') {
      return -blind;
   }

   assert(action == 'h' || action == 'c');

   bool playerWon = state.cards[player] > state.cards[!player];

   if (action == 'h') {
      return (playerWon ? 1 : -1) * blind;
   }

   return (playerWon ? 1 : -1) * (state.betSize + blind);
}

double cfr(CfrState &state, string hist, int player, double reachProb0, double reachProb1)
{
   string terminal;
   string availActions;
   if (hist.empty()) {
      availActions = "bh";
      terminal = "00";
   }
   else {
      switch (hist.back()) {
      case 'b': availActions = "cf"; terminal = "11"; break;
      case 'h': availActions = "h"; terminal = "1"; break;
      default: assert(0); break;
      };
   }

   assert(!availActions.empty());

   auto infoSetKey = getInfosetKey(state, player, hist);

   if (state.strat.find(infoSetKey) == state.strat.end()) {
      auto &snode = state.strat[infoSetKey];
      initStrategy(snode, tostring(state.cards[player]), availActions);

      if (state.lockedStrat.find(infoSetKey) != state.lockedStrat.end()) {
         snode.locked = true;
         snode.strategy = state.lockedStrat[infoSetKey];
      }
   }

   double avgUtil = 0;

   auto &strat = state.strat[infoSetKey];
   regretMatching(strat);
   accumulateAvgStrategy(strat, player == 0 ? reachProb0 : reachProb1);

   vector<double> utilArr(availActions.size(), 0);

   for (int i = 0; i < availActions.size(); ++i) {
      char action = availActions[i];
      bool isTerm = terminal[i] == '1';

      double &util = utilArr[i];
      if (isTerm) {
         util = showdown(state, action, player);
      }
      else {
         double newReachProb0 = reachProb0;
         double newReachProb1 = reachProb1;

         if (player == 0) {
            newReachProb0 *= strat.strategy[i];
         } else {
            newReachProb1 *= strat.strategy[i];
         }

         util = -cfr(state, hist + action, !player, newReachProb0, newReachProb1);
      }

      avgUtil += util * strat.strategy[i];
   }

   for (int i = 0; i < availActions.size(); ++i) {
      double regret = utilArr[i] - avgUtil;
      strat.regrets[i] += (player == 0 ? reachProb1 : reachProb0) * regret;
      strat.regrets[i] = std::max(strat.regrets[i], 0.0);
   }

   return avgUtil;
}

void dumpInfoSets(CfrState &state)
{
   for (auto &stratElem : state.strat) {
      auto &snode = stratElem.second;
      regretMatchingAvgStrategy(snode);

      string strategyStr = stratElem.first + " : ";
      for (int i = 0; i < snode.actionLabels.size(); ++i) {
         char buf[16];
         sprintf(buf, " %c=%3.3f", snode.actionLabels[i], snode.avgStrategy[i]);
         strategyStr += buf;
      }
      cout << strategyStr << endl;
   }
}

int main()
{
   CfrState state;
   initState(state);

   state.cardMask[0] = 0x5;
   state.cardMask[1] = 0x2;

   state.initialPotSize = 4;
   state.betSize = 0.5 * state.initialPotSize;

   //lockNode(state, "", 0, {0.5, 0.5});

   for (int iter = 0; iter < 100000; ++iter) {
      chooseCards(state);
      cfr(state, "", 0, 1.0, 1.0);
   }

   dumpInfoSets(state);

   return 0;
}