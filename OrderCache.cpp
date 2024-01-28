// Your implementation of the OrderCache...
#include "OrderCache.h"

#include <algorithm>

size_t OrderCache::toOrderSide(const std::string& sideStr) {
  std::string lowerCaseSideStr;
  std::transform(sideStr.begin(), sideStr.end(), std::back_inserter(lowerCaseSideStr), ::tolower);

  if (lowerCaseSideStr == "buy") return static_cast<size_t>(OrderSide::BUY);
  if (lowerCaseSideStr == "sell") return static_cast<size_t>(OrderSide::SELL);
  else throw std::exception();
}

void OrderCache::addOrder(Order order) {
  std::lock_guard<std::mutex> lck(mtx);

  auto orderPtr = std::make_shared<Order>(std::move(order));
  m_orderMap[orderPtr->orderId()] = orderPtr;
  m_userIndex[orderPtr->user()][orderPtr->orderId()] = orderPtr;
  m_securityIndex[orderPtr->securityId()][toOrderSide(orderPtr->side())][orderPtr->orderId()] = orderPtr;
}

void OrderCache::cancelOrder(const std::string& orderId) {
  std::lock_guard<std::mutex> lck(mtx);

  const auto& orderMapIt = m_orderMap.find(orderId);
  if (orderMapIt == m_orderMap.end()) {
    return;
  }

  const auto userId = orderMapIt->second->user();
  const auto secId = orderMapIt->second->securityId();
  const auto orderSide = orderMapIt->second->side();

  m_userIndex.at(userId).erase(orderId);
  m_securityIndex.at(secId).at(toOrderSide(orderSide)).erase(orderId);
  m_orderMap.erase(orderId);
}

void OrderCache::cancelOrdersForUser(const std::string& user) {
  std::lock_guard<std::mutex> lck(mtx);

  const auto& userOrderMapIt = m_userIndex.find(user);
  if (userOrderMapIt == m_userIndex.end()) {
    return;
  }

  for (const auto& [orderId, orderWeakPtr]: userOrderMapIt->second) {
    auto orderSharedPtr = orderWeakPtr.lock();
    if (orderSharedPtr) {
      m_securityIndex.at(orderSharedPtr->securityId()).at(toOrderSide(orderSharedPtr->side())).erase(orderId);
    }
    m_orderMap.erase(orderId);
  }
  m_userIndex.erase(user);
}

void OrderCache::cancelOrdersForSecIdWithMinimumQty(const std::string& securityId, unsigned int minQty) {
  std::lock_guard<std::mutex> lck(mtx);
  
  const auto& securityOrderMapIt = m_securityIndex.find(securityId);
  if (securityOrderMapIt == m_securityIndex.end()) {
    return;
  }

  for (auto& ordersBySide : securityOrderMapIt->second) {
    for (auto orderInfoIt = ordersBySide.begin(); orderInfoIt != ordersBySide.end();) {
      auto orderPtr = orderInfoIt->second.lock();
      if (orderPtr && orderPtr->qty() >= minQty) {
        m_userIndex.at(orderPtr->user()).erase(orderPtr->orderId());
        m_orderMap.erase(orderPtr->orderId());
        orderInfoIt = ordersBySide.erase(orderInfoIt);
      } else {
        orderInfoIt++;
      }
    }
  }
}

unsigned int OrderCache::getMatchingSizeForSecurity(const std::string& securityId) {
  unsigned int matchingSize = 0;

  using CompanyName = std::string;
  std::array<std::unordered_map<CompanyName, std::vector<std::weak_ptr<Order>>>, SIDE_SIZE> ordersByCompanies;

  std::lock_guard<std::mutex> lck(mtx);

  for (auto& ordersBySide : m_securityIndex[securityId]) {
    for (auto& [_, orderWptr] : ordersBySide) {
      auto orderPtr = orderWptr.lock();
      if (orderPtr) {
        ordersByCompanies[toOrderSide(orderPtr->side())][orderPtr->company()].emplace_back(orderPtr);
      }
    }
  }

  if (ordersByCompanies[static_cast<size_t>(OrderSide::BUY)].size() == 0 ||
      ordersByCompanies[static_cast<size_t>(OrderSide::SELL)].size() == 0) {
    return 0;
  }

  std::array<std::unordered_map<CompanyName, std::pair<std::vector<std::weak_ptr<Order>>::iterator, int>>, SIDE_SIZE>
      lastVisitedOrder;
  for (size_t i = 0; i < SIDE_SIZE; i++) {
    for (auto& [company, orders] :  ordersByCompanies[i]) {
      lastVisitedOrder[i][company] = std::make_pair(orders.begin(), 0);
    }
  }

  auto buyCompaniesIt = ordersByCompanies[static_cast<size_t>(OrderSide::BUY)].begin();
  auto sellCompaniesIt = ordersByCompanies[static_cast<size_t>(OrderSide::SELL)].begin();
  auto companyBuyOrdersIt = buyCompaniesIt->second.begin();
  auto companySellOrdersIt = sellCompaniesIt->second.begin();

  auto makeValidAdvance = [&](std::vector<std::weak_ptr<Order>>::iterator& it,
      std::unordered_map<std::string, std::vector<std::weak_ptr<Order>>>::iterator& companiesIt,
      const OrderSide& side,
      const std::string& otherSideCompanyName) {
    it++;

    std::decay_t<decltype(it)> notVisitedIt = {};
    std::decay_t<decltype(companiesIt)> notVisitCompanyIt = {};

    size_t rotation = 0;
    while (companiesIt->first == otherSideCompanyName || it == companiesIt->second.end()) {
      companiesIt++;
      if (companiesIt == ordersByCompanies[static_cast<size_t>(side)].end()) {
        companiesIt = ordersByCompanies[static_cast<size_t>(side)].begin();
      }
      auto& lastVisitedOrderForCurrentCompany = lastVisitedOrder[static_cast<size_t>(side)].at(companiesIt->first).first;
      if (lastVisitedOrderForCurrentCompany != companiesIt->second.end()) {
        notVisitCompanyIt = companiesIt;
        notVisitedIt = lastVisitedOrderForCurrentCompany;
      }
      it = lastVisitedOrderForCurrentCompany;

      rotation++;
      if (rotation == ordersByCompanies[static_cast<size_t>(side)].size()) {
        it = notVisitedIt;
        companiesIt = notVisitCompanyIt;
        return false;
      }
    }

    return true;
  };

  auto buyOrderPtr = companyBuyOrdersIt->lock();
  auto sellOrderPtr = companySellOrdersIt->lock();
  if (buyOrderPtr && sellOrderPtr) {
    if (buyOrderPtr->company() == sellOrderPtr->company()) {
      if (!makeValidAdvance(companySellOrdersIt, sellCompaniesIt, OrderSide::SELL, buyCompaniesIt->first)) {
        if (!makeValidAdvance(companyBuyOrdersIt, buyCompaniesIt, OrderSide::BUY, sellCompaniesIt->first)) {
          return 0;
        }
      }
    }
  }

  int32_t qtyRemaining = 0;
  while (true) {
    buyOrderPtr = companyBuyOrdersIt->lock();
    sellOrderPtr = companySellOrdersIt->lock();
    
    if (buyOrderPtr && sellOrderPtr) {
      if (qtyRemaining == 0) {
        qtyRemaining = buyOrderPtr->qty();
      }

      int32_t qtyToBeConsidered = (qtyRemaining > 0 ? sellOrderPtr->qty() : buyOrderPtr->qty());
      const int8_t sign = (qtyRemaining > 0 ? 1 : -1);

      matchingSize += std::min(sign*qtyRemaining, qtyToBeConsidered);

      if ((qtyRemaining - sign*qtyToBeConsidered) >= 0) {
        lastVisitedOrder[static_cast<size_t>(OrderSide::BUY)].at(buyCompaniesIt->first).second =
            qtyRemaining - sign*qtyToBeConsidered;
        lastVisitedOrder[static_cast<size_t>(OrderSide::SELL)].at(sellCompaniesIt->first).first++;
        if (!makeValidAdvance(companySellOrdersIt, sellCompaniesIt, OrderSide::SELL, buyCompaniesIt->first)) {
          if (companySellOrdersIt == std::vector<std::weak_ptr<Order>>::iterator{} || 
              !makeValidAdvance(companyBuyOrdersIt, buyCompaniesIt, OrderSide::BUY, sellCompaniesIt->first)) {
            break;
          }
          qtyRemaining = lastVisitedOrder[static_cast<size_t>(OrderSide::SELL)][sellCompaniesIt->first].second;
          continue;
        }
      }
      if ((qtyRemaining - sign*qtyToBeConsidered) <= 0) {
        lastVisitedOrder[static_cast<size_t>(OrderSide::SELL)].at(sellCompaniesIt->first).second =
            qtyRemaining - sign*qtyToBeConsidered;
        lastVisitedOrder[static_cast<size_t>(OrderSide::BUY)].at(buyCompaniesIt->first).first++;
        if (!makeValidAdvance(companyBuyOrdersIt, buyCompaniesIt, OrderSide::BUY, sellCompaniesIt->first)) {
          if (companyBuyOrdersIt == std::vector<std::weak_ptr<Order>>::iterator{} || 
              !makeValidAdvance(companySellOrdersIt, sellCompaniesIt, OrderSide::SELL, buyCompaniesIt->first)) {
            break;
          }
          qtyRemaining = lastVisitedOrder[static_cast<size_t>(OrderSide::BUY)][buyCompaniesIt->first].second;
          continue;
        }
      }

      qtyRemaining -= sign*qtyToBeConsidered;
    }
  }

  return matchingSize;
}

std::vector<Order> OrderCache::getAllOrders() const {
  std::lock_guard<std::mutex> lck(mtx);

  std::vector<Order> orders;
  std::transform(m_orderMap.begin(), m_orderMap.end(), std::back_inserter(orders),
      [](const auto& pair){
        return *(pair.second);
      });
  return orders;
}