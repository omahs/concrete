"""File containing HDK's intermdiate representation of source programs operations"""

from abc import ABC
from copy import deepcopy
from typing import Any, Dict, Iterable, List, Optional, Tuple

from ..data_types import BaseValue


class IntermediateNode(ABC):
    """Abstract Base Class to derive from to represent source program operations"""

    inputs: List[BaseValue]
    outputs: List[BaseValue]
    op_args: Optional[Tuple[Any, ...]]
    op_kwargs: Optional[Dict[str, Any]]

    def __init__(
        self,
        inputs: Iterable[BaseValue],
        op_args: Optional[Tuple[Any, ...]] = None,
        op_kwargs: Optional[Dict[str, Any]] = None,
    ) -> None:
        self.inputs = list(inputs)
        assert all(map(lambda x: isinstance(x, BaseValue), self.inputs))
        self.op_args = op_args
        self.op_kwargs = op_kwargs

    def is_equivalent_to(self, other: object) -> bool:
        """Overriding __eq__ has unwanted side effects, this provides the same facility without
            disrupting expected behavior too much

        Args:
            other (object): Other object to check against

        Returns:
            bool: True if the other object is equivalent
        """
        return (
            isinstance(other, self.__class__)
            and self.inputs == other.inputs
            and self.outputs == other.outputs
            and self.op_args == other.op_args
            and self.op_kwargs == other.op_kwargs
        )


class Add(IntermediateNode):
    """Addition between two values"""

    def __init__(
        self,
        inputs: Iterable[BaseValue],
        op_args: Optional[Tuple[Any, ...]] = None,
        op_kwargs: Optional[Dict[str, Any]] = None,
    ) -> None:
        assert op_args is None, f"Expected op_args to be None, got {op_args}"
        assert op_kwargs is None, f"Expected op_kwargs to be None, got {op_kwargs}"

        super().__init__(inputs, op_args=op_args, op_kwargs=op_kwargs)
        assert len(self.inputs) == 2

        # For now copy the first input type for the output type
        # We don't perform checks or enforce consistency here for now, so this is OK
        self.outputs = [deepcopy(self.inputs[0])]

    def is_equivalent_to(self, other: object) -> bool:
        return (
            isinstance(other, self.__class__)
            and (self.inputs == other.inputs or self.inputs == other.inputs[::-1])
            and self.outputs == other.outputs
        )


class Input(IntermediateNode):
    """Node representing an input of the numpy program"""

    def __init__(
        self,
        inputs: Iterable[BaseValue],
        op_args: Optional[Tuple[Any, ...]] = None,
        op_kwargs: Optional[Dict[str, Any]] = None,
    ) -> None:
        assert op_args is None, f"Expected op_args to be None, got {op_args}"
        assert op_kwargs is None, f"Expected op_kwargs to be None, got {op_kwargs}"

        super().__init__(inputs, op_args=op_args, op_kwargs=op_kwargs)
        assert len(self.inputs) == 1
        self.outputs = [deepcopy(self.inputs[0])]
