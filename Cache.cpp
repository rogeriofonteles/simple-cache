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
  std::unordered_map<CompanyName, std::vector<std::weak_ptr<Order>>::iterator> it_to_orders;

  for (auto& orders_by_side : security_index_[security_id]) {
    for (auto& [_, order_wptr] : orders_by_side) {
      auto order_ptr = order_wptr.lock();
      if (order_ptr) {
        orders_by_companies[toOrderSide(order_ptr->side())][order_ptr->company()].emplace_back(order_ptr);
      }
    }
  }

  for (auto& orders_by_company : orders_by_companies) {
    for (auto& [company, orders] : orders_by_company) {
      if (company == )
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





