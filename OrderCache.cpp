// Your implementation of the OrderCache...
#include "OrderCache.h"

#include <algorithm>

size_t OrderCache::toOrderSide(const std::string& sideStr) {
  std::string lower_case_side_str;
  std::transform(sideStr.begin(), sideStr.end(), std::back_inserter(lower_case_side_str), ::tolower);

  if (lower_case_side_str == "buy") return static_cast<size_t>(OrderSide::BUY);
  if (lower_case_side_str == "sell") return static_cast<size_t>(OrderSide::SELL);
  else throw std::exception();
}

void OrderCache::addOrder(Order order) {
  std::lock_guard<std::mutex> lck(mtx);

  auto order_ptr = std::make_shared<Order>(std::move(order));
  m_order_map[order_ptr->orderId()] = order_ptr;
  m_user_index[order_ptr->user()][order_ptr->orderId()] = order_ptr;
  m_security_index[order_ptr->securityId()][toOrderSide(order_ptr->side())][order_ptr->orderId()] = order_ptr;
}

void OrderCache::cancelOrder(const std::string& orderId) {
  std::lock_guard<std::mutex> lck(mtx);

  auto user_id = m_order_map[orderId]->user();
  auto sec_id = m_order_map[orderId]->securityId();
  auto order_side = m_order_map[orderId]->side();

  m_user_index[user_id].erase(orderId);
  m_security_index[sec_id][toOrderSide(order_side)].erase(orderId);
  m_order_map.erase(orderId);
}

void OrderCache::cancelOrdersForUser(const std::string& user) {
  std::lock_guard<std::mutex> lck(mtx);

  const auto& user_order_map = m_user_index[user];
  for (const auto& [order_id, order_weak_ptr]: user_order_map) {
    auto order_shared_ptr = order_weak_ptr.lock();
    if (order_shared_ptr) {
      m_security_index[order_shared_ptr->securityId()]
          [toOrderSide(order_shared_ptr->side())].erase(order_id);
    }
    m_order_map.erase(order_id);
  }
  m_user_index.erase(user);
}

void OrderCache::cancelOrdersForSecIdWithMinimumQty(const std::string& securityId, unsigned int minQty) {
  std::lock_guard<std::mutex> lck(mtx);
  
  for (auto& orders_by_side : m_security_index[securityId]) {
    for (auto order_info_it = orders_by_side.begin(); order_info_it != orders_by_side.end();) {
      auto order_ptr = order_info_it->second.lock();
      if (order_ptr && order_ptr->qty() >= minQty) {
        m_user_index[order_ptr->user()].erase(order_ptr->orderId());
        m_order_map.erase(order_ptr->orderId());
        order_info_it = orders_by_side.erase(order_info_it);
      } else {
        order_info_it++;
      }
    }
  }
}

unsigned int OrderCache::getMatchingSizeForSecurity(const std::string& securityId) {
  unsigned int matching_size = 0;

  using CompanyName = std::string;
  std::array<std::unordered_map<CompanyName, std::vector<std::weak_ptr<Order>>>, 2> orders_by_companies;  

  for (auto& orders_by_side : m_security_index[securityId]) {
    for (auto& [_, order_wptr] : orders_by_side) {
      auto order_ptr = order_wptr.lock();
      if (order_ptr) {
        orders_by_companies[toOrderSide(order_ptr->side())][order_ptr->company()].emplace_back(order_ptr);
      }
    }
  }

  if (orders_by_companies[static_cast<size_t>(OrderSide::BUY)].size() == 0 || 
      orders_by_companies[static_cast<size_t>(OrderSide::SELL)].size() == 0) {
    return 0;
  }
  
  std::array<std::unordered_map<CompanyName, std::pair<std::vector<std::weak_ptr<Order>>::iterator, int>>, 2>
      last_visited_order;
  for (size_t i = 0; i < 2; i++) {
    for (auto& [company, orders] :  orders_by_companies[i]) {
      last_visited_order[i][company] = std::make_pair(orders.begin(), 0);
    }
  }

  auto buy_companies_it = orders_by_companies[static_cast<size_t>(OrderSide::BUY)].begin();
  auto sell_companies_it = orders_by_companies[static_cast<size_t>(OrderSide::SELL)].begin();
  auto company_buy_orders_it = buy_companies_it->second.begin();
  auto company_sell_orders_it = sell_companies_it->second.begin();  
  
  auto makeValidAdvance = [&](std::vector<std::weak_ptr<Order>>::iterator& it, 
      std::unordered_map<std::string, std::vector<std::weak_ptr<Order>>>::iterator& companies_it,
      const OrderSide side,
      const std::string& other_side_company_name) {
    it++;

    std::decay_t<decltype(it)> not_visited_it = {};
    std::decay_t<decltype(companies_it)> not_visit_company_it = {};

    size_t rotation = 0;
    while (companies_it->first == other_side_company_name || it == companies_it->second.end()) {
      companies_it++;
      if (companies_it == orders_by_companies[static_cast<size_t>(side)].end()) {
        companies_it = orders_by_companies[static_cast<size_t>(side)].begin();
      }
      if (last_visited_order[static_cast<size_t>(side)][companies_it->first].first != companies_it->second.end()) {
        not_visit_company_it = companies_it;
        not_visited_it = last_visited_order[static_cast<size_t>(side)][companies_it->first].first;        
      }
      it = last_visited_order[static_cast<size_t>(side)][companies_it->first].first;

      rotation++;
      if (rotation == orders_by_companies[static_cast<size_t>(side)].size()) {
        it = not_visited_it;
        companies_it = not_visit_company_it;
       return false;
      }
    }

    return true;
  };

  
  int32_t qty_remaining = 0;
  auto buy_order_ptr = company_buy_orders_it->lock();
  auto sell_order_ptr = company_sell_orders_it->lock();
  if (buy_order_ptr && sell_order_ptr) { 
    if (buy_order_ptr->company() == sell_order_ptr->company()) {      
      if (!makeValidAdvance(company_sell_orders_it, sell_companies_it, OrderSide::SELL, buy_companies_it->first)) {
        if (!makeValidAdvance(company_buy_orders_it, buy_companies_it, OrderSide::BUY, sell_companies_it->first)) {
          return 0;
        }
      }
    }
  }

  while (true) {    
    buy_order_ptr = company_buy_orders_it->lock();
    sell_order_ptr = company_sell_orders_it->lock();
    
    if (buy_order_ptr && sell_order_ptr) { 
      if (qty_remaining == 0) {
        qty_remaining = buy_order_ptr->qty();
      }

      int32_t qty_to_be_considered = (qty_remaining > 0 ? sell_order_ptr->qty() : buy_order_ptr->qty());
      int sign = (qty_remaining > 0 ? 1 : -1);

      matching_size += std::min(sign*qty_remaining, qty_to_be_considered);

      if ((qty_remaining - sign*qty_to_be_considered) >= 0) {
        last_visited_order[static_cast<size_t>(OrderSide::BUY)][buy_companies_it->first].second = 
            qty_remaining - sign*qty_to_be_considered;
        last_visited_order[static_cast<size_t>(OrderSide::SELL)][sell_companies_it->first].first++;
        if (!makeValidAdvance(company_sell_orders_it, sell_companies_it, OrderSide::SELL, buy_companies_it->first)) {
          if (company_sell_orders_it == std::vector<std::weak_ptr<Order>>::iterator{} || 
              !makeValidAdvance(company_buy_orders_it, buy_companies_it, OrderSide::BUY, sell_companies_it->first)) {
            break;
          }
          qty_remaining = last_visited_order[static_cast<size_t>(OrderSide::SELL)][sell_companies_it->first].second;
          continue;
        }
      }
      if ((qty_remaining - sign*qty_to_be_considered) <= 0) {
        last_visited_order[static_cast<size_t>(OrderSide::SELL)][sell_companies_it->first].second = 
            qty_remaining - sign*qty_to_be_considered;
        last_visited_order[static_cast<size_t>(OrderSide::BUY)][buy_companies_it->first].first++;
        if (!makeValidAdvance(company_buy_orders_it, buy_companies_it, OrderSide::BUY, sell_companies_it->first)) {
          if (company_buy_orders_it == std::vector<std::weak_ptr<Order>>::iterator{} || 
              !makeValidAdvance(company_sell_orders_it, sell_companies_it, OrderSide::SELL, buy_companies_it->first)) {
            break;
          }
          qty_remaining = last_visited_order[static_cast<size_t>(OrderSide::BUY)][buy_companies_it->first].second;
          continue;
        }
      }
      
      qty_remaining -= sign*qty_to_be_considered;      
    }
  }

  return matching_size;
}

std::vector<Order> OrderCache::getAllOrders() const {
  std::lock_guard<std::mutex> lck(mtx);

  std::vector<Order> orders;
  std::transform(m_order_map.begin(), m_order_map.end(), std::back_inserter(orders), 
      [](const auto& pair){
        return *(pair.second);
      });
  return orders;
}
