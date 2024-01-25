#pragma once

// C++ includes
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>

class Order;

class Cache
{
public:
  Cache();  
  void addOrderOnCache(Order&& order);

private:
  using UserName = std::string;
  using OrderId = std::string;
  using SecId = std::string;
  // Indexes
  std::unordered_multimap<UserName, std::weak_ptr<Order>> user_index_;
  std::unordered_map<SecId, std::unordered_set<std::weak_ptr<Order>>> security_index_;
  // Order Storage
  std::unordered_multimap<OrderId, std::shared_ptr<Order>> order_map_;
};