// [[Rcpp::plugins(cpp11)]]
#include <Rcpp.h>
#include <fstream>
#include <queue>
#include <stack>
// [[Rcpp::depends(BH)]]
#include <boost/algorithm/string.hpp>
#include <random>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/discrete_distribution.hpp>

using namespace Rcpp;

template<typename T, typename F>
std::vector<F> myMap(std::vector<T> vec, F (*func)(T)) {
  std::vector<F> myOutput;
  
  std::for_each(vec.begin(), vec.end(),
                [&myOutput, &func](const T v){myOutput.push_back(func(v));});
  
  return myOutput;
}

template<typename T>
std::map<T, int> invertedIndex(std::vector<T> vec) {
  std::map<T, int> myOutput;
  
  for (unsigned int i = 0; i < vec.size(); i++) {
    myOutput[vec[i]] = i;
  }
  
  return myOutput;
}

bool BothAreSpaces(char lhs, char rhs) { return (lhs == rhs) && (lhs == ' '); }

std::string toLowerCase(std::string str) {
  boost::algorithm::to_lower(str);
  return str;
}

std::vector<std::string> removeStopWords(const std::vector<std::string> &words, const std::set<std::string> &stopWords) {
  
  std::vector<std::string> updatedWords;
  for (size_t j = 0; j < words.size(); j++) if (stopWords.find(words[j]) == stopWords.end()) updatedWords.push_back(words[j]);
  
  return updatedWords;
}

std::vector<std::string> extractWords(std::string &str) {
  std::string::iterator new_end = std::unique(str.begin(), str.end(), BothAreSpaces);
  str.erase(new_end, str.end());
  
  std::string delimiters(" \r\n\t`~!@#$%^&*()-+=[]{}|\\;:'\",<.>/?");
  std::vector<std::string> words;
  boost::split(words, str, boost::is_any_of(delimiters));
  
  return words;
}


std::vector<std::string> stripWhiteSpaces(const std::vector<std::string> &words) {
  std::vector<std::string> updatedWords;
  for (size_t i = 0; i < words.size(); i++) if (words[i] != "" && words[i] != " ") updatedWords.push_back(words[i]);
  
  return updatedWords;
}

std::vector<std::string> trimWords(std::vector<std::string> &words) {
  for (size_t i = 0; i < words.size(); i++) boost::trim(words[i]);
  
  return words;
}

std::vector<std::vector<std::vector<std::string>>> tokenize(const std::vector<std::vector<std::string>> &textContents, 
                                                            std::vector<std::string> &excludeWords) {
  
  std::vector<std::vector<std::vector<std::string>>> fileFeatures;
  
  excludeWords = myMap(excludeWords, toLowerCase);
  std::set<std::string> stopWords(excludeWords.begin(), excludeWords.end());
  
  for (size_t i = 0; i < textContents.size(); i++) {
    
    std::vector<std::string> contents = textContents[i];
    std::vector<std::vector<std::string>> vec;
    
    int j = 0;
    
    while (j < (int)contents.size()) {
      
      std::vector<std::string> words = extractWords(contents[j]);
      
      words = myMap(words, toLowerCase);
      
      if (!stopWords.empty()) words = removeStopWords(words, stopWords);
      
      words = trimWords(words);
      words = stripWhiteSpaces(words);
      
      vec.push_back(words);
      j++;
    }
    fileFeatures.push_back(vec);
  }

  return fileFeatures;
}

struct TrainInstance {
  int givenWordIndex;
  std::set<int> contextWordsIndices;
};

std::vector<TrainInstance> generateSkipGramTrainingInstances(const std::vector<std::vector<std::vector<std::string>>> &tokenizedWords, 
                                                             std::map<std::string, int> &wordIndex,
                                                             const int &contextSize) {
  
  std::vector<TrainInstance> output;
  
  for (int i = 0; i < tokenizedWords.size(); i++) {
    for (int j = 0; j < tokenizedWords[i].size(); j++) {
      if (tokenizedWords[i][j].size() > 1) {
        std::deque<int> leftIndices, rightIndices;
        
        for (int k = 0; k < tokenizedWords[i][j].size(); k++) {
          
          std::string trainWord = tokenizedWords[i][j][k];
          int trainWordIndex = wordIndex[trainWord];
          
          if (k > contextSize) {
            leftIndices.pop_front();
            leftIndices.push_back(wordIndex[tokenizedWords[i][j][k-1]]);
          }
          else if (k > 0) leftIndices.push_back(wordIndex[tokenizedWords[i][j][k-1]]);
          
          int maxEl = std::min((double)contextSize, (double)tokenizedWords[i][j].size()-1);
          
          if (k == 0) for (int m = 1; m <= maxEl; m++) rightIndices.push_back(wordIndex[tokenizedWords[i][j][m]]);
          
          else if (k + contextSize < tokenizedWords[i][j].size()) {
            rightIndices.pop_front();
            rightIndices.push_back(wordIndex[tokenizedWords[i][j][k + contextSize]]);
          }
          else rightIndices.pop_front();
          
          std::set<int> contextIndices;
          
          for (auto p = leftIndices.begin(); p != leftIndices.end(); ++p) contextIndices.insert(*p);
          for (auto p = rightIndices.begin(); p != rightIndices.end(); ++p) contextIndices.insert(*p);
          
          TrainInstance inst;
          inst.givenWordIndex = trainWordIndex;
          inst.contextWordsIndices = contextIndices;
          
          output.push_back(inst);
        }
      }
    }
  }
  
  return output;
}

void train(std::vector<TrainInstance> &trainInstances, std::map<int, std::vector<double>> &inputWeights, 
           std::map<int, std::vector<double>> &outputWeights, std::map<int, double> &wordProbabilities, 
           const int &negativeSamplesSize, const double &learningRate, const int &numEpochs) {
  
  std::vector<double> wordProbs;
  std::vector<int> wordIndexes;
  
  for (auto p = wordProbabilities.begin(); p != wordProbabilities.end(); ++p) {
    wordIndexes.push_back(p->first);
    wordProbs.push_back(p->second);
  }
  
  boost::random::mt19937 gen;
  
  boost::random::uniform_int_distribution<> uni(0, (int)trainInstances.size()-1);
  boost::random::discrete_distribution<> dist(wordProbs);
  
  int epoch = 1;
  
  while(epoch <= numEpochs) {
    
    std::unordered_map<int, std::unordered_map<int, double>> inputGrads, outputGrads;
    std::random_shuffle (trainInstances.begin(), trainInstances.end());
    
    for (size_t i = 0; i < trainInstances.size(); i++) {
      
      TrainInstance instance = trainInstances[i];
      
      int inputWordIndex = instance.givenWordIndex;
      std::set<int> contextWordIndices = instance.contextWordsIndices;
      
      std::vector<double> inputVector = inputWeights[inputWordIndex];
      
      std::vector<int> sampledIndices;
      
      int j = 1;
      
      while (j <= negativeSamplesSize) {
        int sampledIndex = wordIndexes[dist(gen)];
        
        if (contextWordIndices.find(sampledIndex) == contextWordIndices.end()) {
          sampledIndices.push_back(sampledIndex);
          j++;
        }
      }
      
      std::map<int, double> errors;
      
      for (auto p = contextWordIndices.begin(); p != contextWordIndices.end(); ++p) {
        int contextWordIndex = *p;
        
        std::vector<int> sampledWordIndices(sampledIndices.begin(), sampledIndices.end());
        
        sampledWordIndices.push_back(contextWordIndex);
        
        std::map<int, double> outputs;
        double sumOut = 0;
        
        for (auto q = sampledWordIndices.begin(); q != sampledWordIndices.end(); ++q) {
          std::vector<double> outputVector = outputWeights[*q];
          
          double out = 0;
          
          for (size_t j = 0; j < outputVector.size(); j++) out += outputVector[j]*inputVector[j];
          
          outputs[*q] = 1/(1+exp(-out));
          
          sumOut += outputs[*q];
        }
        
        for (auto q = outputs.begin(); q != outputs.end(); ++q) {
          double prob = (q->second)/sumOut;
          
          double err = prob;
          
          if (q->first == contextWordIndex) err -= 1;
          
          errors[q->first] += err;
        }
      }
      
      std::map<int, double> weightedError;
      
      for (auto p = errors.begin(); p != errors.end(); ++p) {
        for (size_t j = 0; j < inputVector.size(); j++) {
          
          double newLearningRate = learningRate;
          double grad = (p->second)*inputWeights[p->first][j];
          
          outputGrads[p->first][j] += grad*grad;
          
          newLearningRate /= sqrt(outputGrads[p->first][j]);
          outputWeights[p->first][j] -= newLearningRate*grad;
          
          weightedError[j] += (p->second)*outputWeights[p->first][j];
        }
      }
      
      for (size_t j = 0; j < inputVector.size(); j++) {
        double newLearningRate = learningRate;
        double grad = weightedError[j];
        
        inputGrads[inputWordIndex][j] += grad*grad;
        
        newLearningRate /= sqrt(inputGrads[inputWordIndex][j]);
        
        inputWeights[inputWordIndex][j] -= newLearningRate*grad;
      }
    }
    epoch++;
  }
}

std::map<std::string, int> getWordIndices(const std::vector<std::vector<std::vector<std::string>>> &tokenizedWords) {
  std::set<std::string> wordSet;
  
  for (size_t i = 0; i < tokenizedWords.size(); i++) {
    for (size_t j = 0; j < tokenizedWords[i].size(); j++) wordSet.insert(tokenizedWords[i][j].begin(), tokenizedWords[i][j].end());
  }
  
  std::vector<std::string> uniqueWords(wordSet.begin(), wordSet.end());
  std::map<std::string, int> wordIndex = invertedIndex(uniqueWords);
  
  return wordIndex;
}

std::map<int, double> getUnigramDistribution(const std::vector<std::vector<std::vector<std::string>>> &tokenizedWords, 
                                           std::map<std::string, int> wordIndex) {
  
  std::map<int, double> wordCountsMap;

  for (size_t i = 0; i < tokenizedWords.size(); i++) {
    for (size_t j = 0; j < tokenizedWords[i].size(); j++) {
      for (size_t k = 0; k < tokenizedWords[i][j].size(); k++) wordCountsMap[wordIndex[tokenizedWords[i][j][k]]]++;
    }
  }
  
  double sumCounts = 0;
  
  for (auto p = wordCountsMap.begin(); p != wordCountsMap.end(); ++p) {
    double val = pow((double)p->second, 0.75);
    wordCountsMap[p->first] = val;
    sumCounts += val;
  }
  
  for (auto p = wordCountsMap.begin(); p != wordCountsMap.end(); ++p) wordCountsMap[p->first] = (p->second)/sumCounts;
  
  return wordCountsMap;
}

// [[Rcpp::export]]
List cpp__generateWordVectors(std::vector<std::vector<std::string>> textContents, 
                              std::vector<std::string> excludeWords,
                              int vectorSize=300, int contextSize=5, 
                              int negativeSamplesSize=10, double learningRate=0.01, int numEpochs=5) {
  
  std::vector<std::vector<std::vector<std::string>>> tokenizedWords = tokenize(textContents, excludeWords);
  
  std::map<std::string, int> wordIndex = getWordIndices(tokenizedWords);
  std::map<int, double> wordCounts = getUnigramDistribution(tokenizedWords, wordIndex);

  std::vector<TrainInstance> trainInstances = generateSkipGramTrainingInstances(tokenizedWords, wordIndex, contextSize);
  
  std::map<int, std::vector<double>> inputWeights, outputWeights;

  for (int i = 0; i < (int)wordIndex.size(); i++) {
    for (int j = 1; j <= vectorSize; j++) {
      inputWeights[i].push_back(((double) rand() / (RAND_MAX)));
      outputWeights[i].push_back(((double) rand() / (RAND_MAX)));
    }
  }
  
  tokenizedWords.clear();

  train(trainInstances, inputWeights, outputWeights, wordCounts, negativeSamplesSize, learningRate, numEpochs);

  return List::create(_["Words"]=wordIndex, _["WordFreq"]=wordCounts, _["InputVectors"]=inputWeights);
}