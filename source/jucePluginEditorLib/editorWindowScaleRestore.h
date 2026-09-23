#pragma once

namespace jucePluginEditorLib
{
	class EditorWindowScaleRestore
	{
	public:
		struct RootAttachAction
		{
			bool applyConfiguredScale = false;
			bool delayedRestoreArmed = false;
		};

		RootAttachAction attachRoot(const bool _standalone,
			const float _configuredScale)
		{
			if(m_rootAttached)
				return {};

			m_rootAttached = true;
			m_initialScale = _configuredScale;
			m_delayedRestorePending = _standalone && !m_embedded;
			return { !m_embedded, m_delayedRestorePending };
		}

		void setEmbedded(const bool _embedded)
		{
			m_embedded = _embedded;
			if(m_embedded)
				m_delayedRestorePending = false;
		}

		bool consumeDelayedRestore(float& _scale)
		{
			if(!m_delayedRestorePending)
				return false;

			m_delayedRestorePending = false;
			_scale = m_initialScale;
			return true;
		}

		bool shouldPersistResize() const noexcept
		{
			return !m_embedded && !m_delayedRestorePending;
		}

		bool delayedRestorePending() const noexcept
		{
			return m_delayedRestorePending;
		}

		bool isEmbedded() const noexcept { return m_embedded; }

	private:
		float m_initialScale = 100.0f;
		bool m_rootAttached = false;
		bool m_delayedRestorePending = false;
		bool m_embedded = false;
	};
}
