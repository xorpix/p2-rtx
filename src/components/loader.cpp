#include "std_include.hpp"

namespace components
{
	std::vector<component*> loader::components_;
	utils::memory::allocator loader::mem_allocator_;

	void loader::initialize()
	{
		loader::mem_allocator_.clear();
		loader::_register(new main_module());
		loader::_register(new model_render());

		XASSERT(MH_EnableHook(MH_ALL_HOOKS) != MH_STATUS::MH_OK);
	}

	void loader::uninitialize()
	{
		std::ranges::reverse(loader::components_.begin(), loader::components_.end());
		for (const auto component : loader::components_)
		{
			delete component;
		}

		loader::components_.clear();
		loader::mem_allocator_.clear();
		fflush(stdout);
		fflush(stderr);
	}

	void loader::_register(component* component)
	{
		if (component)
		{
			game::loaded_modules.push_back("component registered: "s + component->get_name() + "\n");
			loader::components_.push_back(component);
		}
	}

	utils::memory::allocator* loader::get_alloctor()
	{
		return &loader::mem_allocator_;
	}
}
