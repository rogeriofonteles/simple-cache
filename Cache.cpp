#include "OrderCache.h"

#include <algorithm>

size_t Cache::toOrderSide(const std::string side_str) {
  if (side_str == "Buy") return static_cast<size_t>(OrderSide::BUY);
  if (side_str == "Sell") return static_cast<size_t>(OrderSide::SELL);
  else throw std::exception();
}

void Cache::addOrderOnCache(Order&& order) {
  std::lock_guard<std::mutex> lck(mtx_);

  auto order_ptr = std::make_shared<Order>(std::move(order));
  order_map_[order.orderId()] = order_ptr;
  user_index_[order.user()][order.orderId()] = order_ptr;
  security_index_[order.securityId()][toOrderSide(order.side())][order.orderId()] = order_ptr;
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
  unsigned int matching_size;

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
  
  std::unordered_map<CompanyName, std::vector<std::weak_ptr<Order>>::iterator> last_visited_sell_order;
  for (auto& [company, orders] :  orders_by_companies[static_cast<size_t>(OrderSide::SELL)]) {
    last_visited_sell_order[company] = orders.begin();
  }

  auto buy_companies_it = orders_by_companies[static_cast<size_t>(OrderSide::BUY)].begin();
  auto sell_companies_it = orders_by_companies[static_cast<size_t>(OrderSide::SELL)].begin();
  auto company_buy_orders_it = buy_companies_it->second.begin();
  auto company_sell_orders_it = sell_companies_it->second.begin();  
  
  auto makeValidAdvance = [&](std::vector<std::weak_ptr<Order>>::iterator& it, 
      std::unordered_map<std::string, std::vector<std::weak_ptr<Order>>>::iterator& companies_it,
      const OrderSide side,
      const bool check_for_company_name,      
      const std::string& buy_company = "") {
    size_t rotation = 0;
    while ((check_for_company_name && companies_it->first == buy_company) || 
        last_visited_sell_order[companies_it->first] == companies_it->second.end()) {
      if (companies_it == orders_by_companies[static_cast<size_t>(side)].end()) {
        companies_it = orders_by_companies[static_cast<size_t>(side)].begin();
      } else {
        companies_it++;
      }
      
      rotation++;
      if (rotation == orders_by_companies.size()) {
       return false;
      }
    }

    it = last_visited_sell_order[companies_it->first];
    return true;
  };

  
  int32_t qty_remaining = 0;
  auto buy_order_ptr = company_buy_orders_it->lock();
  auto sell_order_ptr = company_sell_orders_it->lock();
  if (buy_order_ptr && sell_order_ptr) { 
    if (buy_order_ptr->company() == sell_order_ptr->company()) {
      auto is_valid = makeValidAdvance(company_sell_orders_it, sell_companies_it, OrderSide::SELL, true, sell_companies_it->first);
      if (!is_valid) {
        return 0;
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

      unsigned int qty_to_be_considered = (qty_remaining > 0 ? sell_order_ptr->qty() : buy_order_ptr->qty());
      int sign = (qty_remaining > 0 ? 1 : -1);

      matching_size += std::min(static_cast<unsigned int>(sign*qty_remaining), qty_to_be_considered);

      if (qty_remaining - sign*qty_to_be_considered >= 0) {
        if (!makeValidAdvance(company_sell_orders_it, sell_companies_it, OrderSide::SELL, true, sell_companies_it->first)) {
          break;
        }
      }
      if (qty_remaining - sign*qty_to_be_considered <= 0) {
        if (!makeValidAdvance(company_buy_orders_it, buy_companies_it, OrderSide::BUY, false)) {
          break;
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
  std::transform(order_map_.begin(), order_map_.end(), orders.end(), 
      [](const auto& pair){
        return *pair.second;
      });
  return orders;
}





