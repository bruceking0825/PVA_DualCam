from __future__ import annotations

from typing import ClassVar

from PySide6.QtWidgets import QWidget


class BasePage(QWidget):
    """统一所有主页面的创建顺序，并在类定义阶段检查页面契约。"""

    UI_CLASS: ClassVar[type | None] = None
    REQUIRED_HOOKS: ClassVar[tuple[str, ...]] = (
        "_init_state",
        "_setup_ui",
        "_bind_events",
        "_bind_signals",
    )

    def __init_subclass__(cls, **kwargs) -> None:
        super().__init_subclass__(**kwargs)
        if "__init__" in cls.__dict__:
            raise TypeError(f"{cls.__name__} must not override BasePage.__init__()")
        if cls.__dict__.get("UI_CLASS") is None:
            raise TypeError(f"{cls.__name__} must define UI_CLASS")
        missing = [name for name in cls.REQUIRED_HOOKS if name not in cls.__dict__]
        if missing:
            raise TypeError(f"{cls.__name__} must define: {', '.join(missing)}")

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._init_state()
        self.ui = self.UI_CLASS()
        self.ui.setupUi(self)
        self._setup_ui()
        self._bind_events()
        self._bind_signals()
        self._on_ready()

    def _init_state(self) -> None:
        raise NotImplementedError

    def _setup_ui(self) -> None:
        raise NotImplementedError

    def _bind_events(self) -> None:
        raise NotImplementedError

    def _bind_signals(self) -> None:
        raise NotImplementedError

    def _on_ready(self) -> None:
        pass
