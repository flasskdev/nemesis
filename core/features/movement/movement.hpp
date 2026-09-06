#pragma once

namespace features::movement {

    class airstrafe
    {
    public:
        void store_angles();
        void on_create_move(systems::input::usercmd* cmd);
        void finalize(systems::input::usercmd* cmd);
        [[nodiscard]] bool active_this_tick() const { return m_active_this_tick; }
    private:
        math::vector3 m_input_angles{};
        std::uintptr_t m_input_buttons{};
        float m_target_yaw{};
        bool m_input_valid{};
        bool m_active_this_tick{};
        bool m_braking{};
    };

	class bhop
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class jumpbug
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		[[nodiscard]] bool active_this_tick( ) const { return this->m_active_this_tick; }
		[[nodiscard]] float landing_fraction( ) const { return this->m_landing_fraction; }

	private:
		[[nodiscard]] float get_impulse_mul( std::uintptr_t local_pawn ) const;

		float m_landing_fraction{ 1.0f };
		bool m_active_this_tick{ false };
	};

	class fastladder
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class edgejump
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class edgestop
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class edgebug
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		void on_render( xdraw::draw_list& draw_list );

		[[nodiscard]] bool active_this_tick( ) const { return this->m_active_this_tick; }

	private:
		bool m_active_this_tick{ false };
	};

	class slowwalk
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class test_strafer
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		[[nodiscard]] bool is_active( ) const;
		[[nodiscard]] bool handled_this_tick( ) const { return this->m_handled_this_tick; }

	private:

		bool m_handled_this_tick{};
	};

} // namespace features::movement