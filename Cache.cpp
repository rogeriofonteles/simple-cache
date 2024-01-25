#include "OrderCache.h"

#include <algorithm>

void Cache::addOrderOnCache(Order&& order) {
  std::lock_guard<std::mutex> lck(mtx_);

  auto order_ptr = std::make_shared<Order>(std::move(order));
  order_map_[order.orderId()] = order_ptr;
  user_index_[order.user()][order.orderId()] = order_ptr;
  security_index_[order.securityId()][order.side()][order.orderId()] = order_ptr;
}

void Cache::removeFromCacheByOrderId(const OrderId& order_id) {
  std::lock_guard<std::mutex> lck(mtx_);

  auto user_id = order_map_[order_id]->user();
  auto sec_id = order_map_[order_id]->securityId();
  auto order_side = order_map_[order_id]->side();

  user_index_[user_id].erase(order_id);
  security_index_[sec_id][order_side].erase(order_id);
  order_map_.erase(order_id);
}

void Cache::removeFromCacheByUserName(const UserName& user_name) {
  std::lock_guard<std::mutex> lck(mtx_);

  const auto& user_order_map = user_index_[user_name];
  for (const auto& [order_id, order_weak_ptr]: user_order_map) {
    auto order_shared_ptr = order_weak_ptr.lock();
    if (order_shared_ptr) {
      security_index_[order_shared_ptr->securityId()][order_shared_ptr->side()].erase(order_id);
    }
    order_map_.erase(order_id);
  }
  user_index_.erase(user_name);
}

// TODO: Use enum for side
void Cache::removeFromCacheBySecId(const SecId& security_id, unsigned int min_qty) {
  std::lock_guard<std::mutex> lck(mtx_);

  std::array<decltype(security_index_[security_id]["Buy"])& , 2> orders_by_side = 
      { security_index_[security_id]["Buy"], security_index_[security_id]["Sell"] };

  for (int i = 0; i < 2; i++) {
    auto orders_it = std::find_if(
      orders_by_side[i].begin(),
      orders_by_side[i].end(), 
      [](const auto& wptr) {
        auto ptr = wptr.lock();
        if (ptr) {
          return ptr->qty() >= min_qty;
        }
      });
    
    for (auto it = orders_it; it != orders_by_side[i].end(); it++) {
      auto order_ptr = it->second.lock();
      if (order_ptr) {
        user_index_[order_ptr->user()].erase(order_ptr->orderId());
        order_map_.erase(order_ptr->orderId());
      }
    }
    orders_by_side[i].erase(orders_it, orders_by_side[i].end());
  }
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





