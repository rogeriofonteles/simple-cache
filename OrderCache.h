#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Order {
   public:
    Order(const std::string& ordId, const std::string& secId, const std::string& side, const unsigned int qty,
          const std::string& user, const std::string& company)
        : m_orderId(ordId), m_securityId(secId), m_side(side), m_qty(qty), m_user(user), m_company(company) {}

    std::string orderId() const { return m_orderId; }
    std::string securityId() const { return m_securityId; }
    std::string side() const { return m_side; }
    std::string user() const { return m_user; }
    std::string company() const { return m_company; }
    unsigned int qty() const { return m_qty; }

   private:
    std::string m_orderId;
    std::string m_securityId;
    std::string m_side;
    unsigned int m_qty;
    std::string m_user;
    std::string m_company;
};

class OrderCacheInterface {
   public:
    // add order to the cache
    virtual void addOrder(Order order) = 0;

    // remove order with this unique order id from the cache
    virtual void cancelOrder(const std::string& orderId) = 0;

    // remove all orders in the cache for this user
    virtual void cancelOrdersForUser(const std::string& user) = 0;

    // remove all orders in the cache for this security with qty >= minQty
    virtual void cancelOrdersForSecIdWithMinimumQty(const std::string& securityId, unsigned int minQty) = 0;

    // return the total qty that can match for the security id
    virtual unsigned int getMatchingSizeForSecurity(const std::string& securityId) = 0;

    // return all orders in cache in a vector
    virtual std::vector<Order> getAllOrders() const = 0;
};

constexpr const size_t SIDE_SIZE = 2;

class OrderCache : public OrderCacheInterface {
    using UserName = std::string;
    using OrderId = std::string;
    using SecId = std::string;

   public:
    enum class OrderSide { BUY = 0, SELL };

    void addOrder(Order order) override;

    void cancelOrder(const std::string& orderId) override;

    void cancelOrdersForUser(const std::string& user) override;

    void cancelOrdersForSecIdWithMinimumQty(const std::string& securityId, unsigned int minQty) override;

    unsigned int getMatchingSizeForSecurity(const std::string& securityId) override;

    std::vector<Order> getAllOrders() const override;

   private:
    size_t toOrderSide(const std::string& sideStr);

    // Indexes
    std::unordered_map<UserName, std::unordered_map<OrderId, std::weak_ptr<Order>>> m_userIndex;
    std::unordered_map<SecId, std::array<std::unordered_map<OrderId, std::weak_ptr<Order>>, SIDE_SIZE>> m_securityIndex;
    // Order Storage
    std::unordered_map<OrderId, std::shared_ptr<Order>> m_orderMap;
    // Mutex
    mutable std::mutex mtx;
};