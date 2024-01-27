#include "OrderCache.h"

#include <algorithm>

size_t Cache::toOrderSide(const std::string side_str) {
  std::string _lower_case_side_str;
  std::transform(side_str.begin(), side_str.end(), std::back_inserter(_lower_case_side_str), ::tolower);

  if (_lower_case_side_str == "buy") return static_cast<size_t>(OrderSide::BUY);
  if (_lower_case_side_str == "sell") return static_cast<size_t>(OrderSide::SELL);
  else throw std::exception();
}

void Cache::addOrderOnCache(Order&& order) {
  std::lock_guard<std::mutex> lck(mtx_);

  auto order_ptr = std::make_shared<Order>(std::move(order));
  order_map_[order_ptr->orderId()] = order_ptr;
  user_index_[order_ptr->user()][order_ptr->orderId()] = order_ptr;
  security_index_[order_ptr->securityId()][toOrderSide(order_ptr->side())][order_ptr->orderId()] = order_ptr;
}

void Cache::removeFromCacheByOrderId(const OrderId& order_id) {
  std::lock_guard<std::mutex> lck(mtx_);

  auto user_id = order_map_[order_id]->user();
  auto sec_id = order_map_[order_id]->securityId();
  auto order_side = order_map_[order_id]->side();

  user_index_[user_id].erase(order_id);
  security_index_[sec_id][toOrderSide(order_side)].erase(order_id);
  order_map_.erase(order_id);
}

void Cache::removeFromCacheByUserName(const UserName& user_name) {
  std::lock_guard<std::mutex> lck(mtx_);

  const auto& user_order_map = user_index_[user_name];
  for (const auto& [order_id, order_weak_ptr]: user_order_map) {
    auto order_shared_ptr = order_weak_ptr.lock();
    if (order_shared_ptr) {
      security_index_[order_shared_ptr->securityId()]
          [toOrderSide(order_shared_ptr->side())].erase(order_id);
    }
    order_map_.erase(order_id);
  }
  user_index_.erase(user_name);
}

void Cache::removeFromCacheBySecId(const SecId& security_id, unsigned int min_qty) {
  std::lock_guard<std::mutex> lck(mtx_);
  
  for (auto& orders_by_side : security_index_[security_id]) {
    for (auto order_info_it = orders_by_side.begin(); order_info_it != orders_by_side.end();) {
      auto order_ptr = order_info_it->second.lock();
      if (order_ptr && order_ptr->qty() >= min_qty) {
        user_index_[order_ptr->user()].erase(order_ptr->orderId());
        order_map_.erase(order_ptr->orderId());
        order_info_it = orders_by_side.erase(order_info_it);
      } else {
        order_info_it++;
      }
    }
  }
}

unsigned int Cache::getMatchingSizeForSecurity(const SecId& security_id) {
  unsigned int matching_size = 0;

  using CompanyName = std::string;
  std::array<std::unordered_map<CompanyName, std::vector<std::weak_ptr<Order>>>, 2> orders_by_companies;  

  for (auto& orders_by_side : security_index_[security_id]) {
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
  
  // TODO: check here!
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

std::vector<Order> Cache::getOrdersFromCacheAsVec() const {
  std::lock_guard<std::mutex> lck(mtx_);

  std::vector<Order> orders;
  std::transform(order_map_.begin(), order_map_.end(), std::back_inserter(orders), 
      [](const auto& pair){
        return *(pair.second);
      });
  return orders;
}





