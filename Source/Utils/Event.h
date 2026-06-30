// ###############################################################################################################
// Copyright 2018, Jakub Mrowinski, All rights reserved.
// ###############################################################################################################
#pragma once
#include <vector>
#include <algorithm>
#include <memory>
#include <any>

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template<typename ObjectType, typename ReturnType, typename... Args>
using Ptr2Member = ReturnType(ObjectType::*)(Args...);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This abstracts away from ObjectType
template<typename ReturnType, typename... Args>
struct IDelegate
{
	virtual ReturnType Call(Args...) const = 0;
	virtual bool operator==(const IDelegate&) const = 0;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stores data about callee and the function to call
template<typename ObjectType, typename ReturnType, typename... Args>
struct MemberDelegate : IDelegate<ReturnType, Args...>
{
	MemberDelegate(ObjectType* _object, Ptr2Member<ObjectType, ReturnType, Args...> _func_ptr, int _priority = 0)
		: object(_object)
		, func_ptr(_func_ptr)
		, priority(_priority)
	{}

	virtual ReturnType Call(Args... args) const override
	{
		return (object->*func_ptr)(std::forward<Args>(args)...);
	}

	Ptr2Member<ObjectType, ReturnType, Args...> func_ptr = nullptr;
	ObjectType* object = nullptr;
	int priority;

	virtual bool operator==(const IDelegate<ReturnType, Args...>& other) const override
	{
		auto* casted_other = dynamic_cast<decltype(this)>(&other);

		// Ignore priority
		return casted_other
			&& func_ptr == casted_other->func_ptr
			&& object == casted_other->object;
	}
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Interface for cleaner Connection
class IEvent
{
public:
	virtual void RemoveListener(class Connection&) = 0;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Use this to unbind from an event.
class [[nodiscard]] Connection
{
public:
	Connection() = default;
	virtual ~Connection() = default;

	template<typename... Args>
	Connection(std::shared_ptr<IEvent> event, IDelegate<void, Args...>* data)
		: m_Event(event)
		, m_Func(data)
	{}

	// Copying connections is forbidden
	Connection(const Connection&) = delete;

	// Moving is fine
	Connection(Connection&& other) noexcept
		: m_Event(std::move(other.m_Event))
		, m_Func(other.m_Func)
	{}

	// Move assignment is also fine
	Connection& operator=(Connection&& other)
	{
		m_Event = std::move(other.m_Event);
		m_Func = other.m_Func;
		return *this;
	}

	// Disable the connection.
	void Disconnect()
	{
		if (m_Event && m_IsActive)
			m_Event->RemoveListener(*this);

		m_IsActive = false;
	}

	// Easy checking
	explicit operator bool() const
	{
		return static_cast<bool>(m_Event) && m_IsActive;
	}

protected:
	std::shared_ptr<IEvent> m_Event;
	std::any m_Func;
	bool m_IsActive = true;

	template<typename... Args>
	friend class Event;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RAII version for convenience.
class [[nodiscard]] ScopedConnection : public Connection
{
public:
	ScopedConnection() = default;

	ScopedConnection(Connection&& conn)
		: Connection(std::move(conn))
	{}

	ScopedConnection(ScopedConnection&& conn) noexcept
		: Connection(std::move(conn))
	{}

	ScopedConnection& operator=(ScopedConnection&& other) noexcept
	{
		m_Event = std::move(other.m_Event);
		m_Func = other.m_Func;
		return *this;
	}

	~ScopedConnection()
	{
		Connection::Disconnect();
	}
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Broadcasts data to bound delegates.
template<typename... Args>
class Event : public IEvent
{
public:
	Event() = default;

	// Registers a new delegate.
	template<typename ObjectType>
	IDelegate<void, Args...>* AddListener(ObjectType* object, Ptr2Member<ObjectType, void, Args...> func_ptr, int priority = 0)
	{
		using FuncType = MemberDelegate<ObjectType, void, Args...>;

		if (m_IsBroadcasting)
			return nullptr;

		// Create a reference for search op.
		auto func = FuncType(object, func_ptr, priority);

		// Do not add duplicates.
		auto found_it = std::find_if(m_DelegateData.begin(), m_DelegateData.end(), [&func](const auto& data)
		{
			return (*data) == func;
		});

		if (found_it != m_DelegateData.end())
			return nullptr;
		
		// Maintain order by priority.
		auto insert_it = std::find_if(m_DelegateData.begin(), m_DelegateData.end(), [&func](const auto& data)
		{
			return static_cast<FuncType&>(*data).priority < func.priority;
		});

		size_t insert_idx = insert_it - m_DelegateData.begin();
		m_DelegateData.emplace(insert_it, std::make_unique<FuncType>(func));
		return m_DelegateData[insert_idx].get();
	}

	// Unbinds a delegate managed by a Connection.
	virtual void RemoveListener(Connection& conn) override
	{
		if (m_IsBroadcasting)
			return;

		auto* func = std::any_cast<IDelegate<void, Args...>*>(conn.m_Func);
		auto found_it = std::find_if(m_DelegateData.begin(), m_DelegateData.end(), [&func](const auto& data)
		{
			return (*data) == (*func);
		});

		if (found_it != m_DelegateData.end())
			m_DelegateData.erase(found_it);
	}

	// Calls all delegates.
	// While broadcasting, disable adding/removing listeners.
	void Trigger(Args... args) const
	{
		m_IsBroadcasting = true;

		for (auto&& data : m_DelegateData)
			data->Call(args...);

		m_IsBroadcasting = false;
	}

	std::vector<std::unique_ptr<IDelegate<void, Args...>>> m_DelegateData;
	mutable bool m_IsBroadcasting = false;;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Manages events on global level.
template<typename ID>
class EventManager final
{
	template<typename T>
	using raw_t = std::remove_cv_t<std::remove_reference_t<T>>;

public:
	// Singleton
	static EventManager& GetInstance()
	{
		static EventManager s_Instance;
		return s_Instance;
	}

	void BlockEvents(bool b)
	{
		m_EventBlock = b;
	}

	// Binds signature to ID. 
	template<ID id, typename... Args>
	void DeclareEvent(bool allow_redefine = false)
	{
		auto& slot = m_Events[static_cast<int>(id)];

		if (!slot.has_value() || allow_redefine)
		{
			slot = std::make_shared<Event<raw_t<Args>...>>();
		}
	}

	// Bind a pointer to member function to an event under id.
	// Will fail if the signatures don't match.
	template<ID id, typename ObjectType, typename... Args>
	Connection AddEventListener(ObjectType* object, Ptr2Member<ObjectType, void, Args...> func_ptr, int priority = 0)
	{
		auto& slot = m_Events[static_cast<int>(id)];
		using EventType = std::shared_ptr<Event<raw_t<Args>...>>;

		if (!slot.has_value() || slot.type() != typeid(EventType))
			return Connection();

		auto& casted_slot = std::any_cast<EventType&>(slot);
		auto* created_func = casted_slot->AddListener(object, func_ptr, priority);
		if(!created_func)
			return Connection();

		return Connection(casted_slot, created_func);
	}

	// Calls all delegates bound to an event under id.
	// Will fail if the signatures don't match.
	template<ID id, typename... Args>
	bool TriggerEvent(Args... args)
	{
		if (m_EventBlock)
			return false;

		auto& slot = m_Events[static_cast<int>(id)];
		using EventType = std::shared_ptr<Event<raw_t<Args>...>>;

		if (!slot.has_value() || slot.type() != typeid(EventType))
			return false;

		auto& casted_slot = std::any_cast<EventType&>(slot);
		casted_slot->Trigger(std::forward<Args>(args)...);
		return true;
	}

	// For memory leak test purposes.
	void DestroyAll()
	{
		for (int i = 0; i < static_cast<int>(ID::Count); ++i)
			m_Events[i].reset();
	}

private:
	std::any m_Events[static_cast<int>(ID::Count)];
	bool m_EventBlock = false;
};

